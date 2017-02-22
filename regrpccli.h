#pragma once
#include <cstd/std.h>
#include <cstruct/binary_tree.h>
#include <fixedstr/fixedstr.h>
#include <relay/relay_client.h>

struct regrpccli {
	struct relay_client *client;
	struct binary_tree nodereg;
	float timeout;
};

typedef bool regrpccli_observer(struct regrpccli *inst, const struct fstr *node, const struct fstr *key, const struct fstr *value);

void regrpccli_init(struct regrpccli *inst, struct relay_client *client, float timeout);

/* Clear cached data */
void regrpccli_purge(struct regrpccli *inst);
void regrpccli_purge_node(struct regrpccli *inst, const struct fstr *nodename);
void regrpccli_purge_reg(struct regrpccli *inst, const struct fstr *regname);

bool regrpccli_list(struct regrpccli *inst, const struct fstr *node /* TODO tree copy */);
bool regrpccli_get_cached(struct regrpccli *inst, const struct fstr *node, const struct fstr *key, struct fstr *value);
bool regrpccli_get(struct regrpccli *inst, const struct fstr *node, const struct fstr *key, struct fstr *value);
bool regrpccli_set(struct regrpccli *inst, const struct fstr *node, const struct fstr *key, const struct fstr *value);
bool regrpccli_subscribe(struct regrpccli *inst, const struct fstr *node, const struct fstr *key, float min_interval);
bool regrpccli_unsubscribe(struct regrpccli *inst, const struct fstr *node, const struct fstr *key);

void regrpccli_destroy(struct regrpccli *inst);
