#if 0
(
set -eu
declare -r tmp='./regrpccli'
declare -r addr="${addr:-:: 3031}"
if [ "${1:-}" == 'cli' ]; then
	sleep 1
	declare -r svr=rpctestsvr
	declare -r cli=rpctestcli
	printf "%s\n" "$svr" "Key=Value" "Melon=Lemon" "ABORT" "$svr" "Command=nonsense" "Potato=Tomato" "SEND" "QUIT" | \
	(valgrind --quiet --leak-check=full --track-origins=yes \
		"$tmp" -vvv $addr "$cli" && sleep 3
	) || read line
else
	gcc -DREGRPCCLI -Wall -Wextra -Werror -Ic_modules -DSIMPLE_LOGGING -O2 -g -std=gnu11 -o "$tmp" $(find -name '*.c') -lpthread
	(cd c_modules/relay && npm install)
	tmux split-window -dv bash "$0" cli
	node c_modules/relay/server.js
fi
)
exit 0
#endif
#include <cstd/std.h>
#include <cstd/unix.h>
#include <relay/relay_client.h>
#include <getdelim/getdelim.h>
#include <fixedstr/fixedstr.h>
#include <keystore/keystore.h>
#include <regrpc/regstore_rpc_defs.h>

static void show_help(const char *name)
{
	printf("Syntax: %s [-0iv] <address> <port> <name>\n"
		"\t-0\tInput and output lines are NULL-terminated\n"
		"\t-i\tInteractive mode (log more, do not quit on error)\n"
		"\t-v\tIncrease verbosity\n"
		"\n", name);
}

static uint32_t next_seq()
{
	static uint32_t _seq = 0;
	return ++_seq;
}

static char delim = '\n';
static int verbosity;
static struct relay_client client;
static pthread_t rx_tid;

#define RECV_END "END"
#define CMD_SEND "SEND"
#define CMD_ABORT "ABORT"
#define CMD_QUIT "QUIT"
#define KV_DELIM '='

static void *rx_thread(void *arg)
{
	(void) arg;
	struct relay_packet *p;
	while (relay_client_recv_packet(&client, &p)) {
		struct fstr msg = FSTR_INIT;
		fstr_format_append(&msg, "%s%c", p->remote, delim);
		const char *type;
		if (strcmp(p->type, DATA_REQUEST) == 0) {
			type = "Request";
		} else if (strcmp(p->type, DATA_RESPONSE) == 0) {
			type = "Response";
		} else if (strcmp(p->type, DATA_NOTIFY) == 0) {
			type = "Notification";
		} else {
			type = p->type;
		}
		fstr_format_append(&msg, "[%s]%c", type, delim);
		struct keystore ks;
		keystore_init_from(&ks, 1024, p->data, p->length);
		struct keystore_iterator it;
		struct fstr key;
		struct fstr val;
		keystore_iterator_init(&it, &ks);
		while (keystore_iterator_next_pair_f(&it, &key, &val)) {
			fstr_format_append(&msg, PRIfs "%c" PRIfs "%c", prifs(&key), KV_DELIM, prifs(&val), delim);
		}
		keystore_iterator_destroy(&it);
		keystore_destroy(&ks);
		fstr_format_append(&msg, "%s%c", RECV_END, delim);
		printf(PRIfs, prifs(&msg));
		fstr_destroy(&msg);
		free(p);
	}
	return NULL;
}

#if defined REGRPCCLI
int main(int argc, char *argv[])
{
	bool interactive = false;
	const char *prog_name = argv[0];
	int ch;
	while ((ch = getopt(argc, argv, "0iv")) != -1) {
		switch (ch) {
		case '0': delim = 0; break;
		case 'i': interactive = true; break;
		case 'v': verbosity++; break;
		case '?': show_help(prog_name); return 1;
		}
	}
	if (optind + 3 != argc) {
		show_help(prog_name);
		return 1;
	}
	const char *addr = argv[optind+0];
	const char *port = argv[optind+1];
	const char *name = argv[optind+2];
	if (!relay_client_init_socket(&client, name, addr, port)) {
		log_error("Failed to initialise relay client");
		return 2;
	}
	if (pthread_create(&rx_tid, NULL, rx_thread, NULL) != 0) {
		log_error("Failed to start receiver thread");
		return 3;
	}
	/* TODO: Receiver */
	struct fstr target = FSTR_INIT;
	struct fstr kv = FSTR_INIT;
	int count = 0;
	bool quit = false;
	while (!quit && fstr_getdelim(&target, delim, stdin) && !fstr_eq2(&target, CMD_QUIT)) {
		if (verbosity >= 2) {
			log_info("Read target <" PRIfs ">", prifs(&target));
		}
		struct keystore ks;
		keystore_init(&ks, 1024, 1024);
		bool send = false;
		bool abort = false;
		int id = count++;
		uint32_t seq = next_seq();
		fstr_format(&kv, "%s%c%" PRIu32, REG_PARAM_SEQ, KV_DELIM, seq);
		keystore_write(&ks, fstr_get(&kv), fstr_len(&kv) + 1);
		while (fstr_getdelim(&kv, delim, stdin)) {
			if (verbosity >= 2) {
				log_info("Read keyval <" PRIfs ">", prifs(&kv));
			}
			if (fstr_eq2(&kv, CMD_SEND)) {
				if (verbosity >= 2) {
					log_info("Sending");
				}
				send = true;
				break;
			}
			if (fstr_eq2(&kv, CMD_ABORT)) {
				if (verbosity >= 2) {
					log_info("Aborting");
				}
				abort = true;
				break;
			}
			if (fstr_eq2(&kv, CMD_QUIT)) {
				if (verbosity >= 2) {
					log_info("Quitting");
				}
				quit = true;
				break;
			}
			char *eq = memchr(fstr_get(&kv), KV_DELIM, fstr_len(&kv));
			if (!eq || *eq != KV_DELIM) {
				log_error("Could not find key/value separator '%c' in line <" PRIfs ">", KV_DELIM, prifs(&kv));
				if (!interactive) {
					quit = true;
					break;
				}
			}
			/* Null-terminator is implicit with fixedstr */
			keystore_write(&ks, fstr_get(&kv), fstr_len(&kv) + 1);
		}
		if (verbosity >= 1) {
			if (send) {
				log_info("Action: send");
			}
			if (quit) {
				log_info("Action: quit");
			}
			if (abort) {
				log_info("Action: abort");
			}
		}
		if (send) {
			struct fstr data = FSTR_INIT;
			keystore_data_f(&ks, &data);
			if (verbosity >= 3) {
				log_info("Attempting to send to <" PRIfs ">", prifs(&target));
				keystore_print(&ks, '=', '\n', NULL);
			}
			if (!relay_client_send_packet(&client, DATA_REQUEST, fstr_get(&target), fstr_get(&data), fstr_len(&data))) {
				log_error("Failed to send message #%d", id);
				if (!interactive) {
					quit = true;
				}
			}
			fstr_destroy(&data);
		}
		keystore_destroy(&ks);
		if (!quit && !send && !abort) {
			log_error("Missing command following last message");
			break;
		}
	}
	fstr_destroy(&kv);
	fstr_destroy(&target);
	pthread_cancel(rx_tid);
	pthread_join(rx_tid, NULL);
	relay_client_destroy(&client);
	return 0;
}
#endif

