/*
 * Node.js `diagnostics_channel` module for xbintsc.
 *
 * `channel(name)` / `tracingChannel(name)` return a channel object with the
 * standard surface (`subscribe` / `unsubscribe` / `publish` / `hasSubscribers`).
 * There is no real tracing backend, so subscriptions are dropped and publish is
 * a no-op returning false.
 */

#include "../node_common.h"

static xt_value channel_subscribe(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

static xt_value channel_unsubscribe(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  (void)argc;
  (void)argv;
  return XT_FALSE;
}

static xt_value channel_publish(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  (void)argc;
  (void)argv;
  return XT_FALSE;
}

static xt_value channel_has_subscribers(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  (void)argc;
  (void)argv;
  return XT_FALSE;
}

static xt_value diagnostics_channel_object(void) {
  xt_value channel = xt_object_new();
  xt_node_set(channel, "subscribe", xt_closure_new((void *)channel_subscribe, 0, NULL));
  xt_node_set(channel, "unsubscribe", xt_closure_new((void *)channel_unsubscribe, 0, NULL));
  xt_node_set(channel, "publish", xt_closure_new((void *)channel_publish, 0, NULL));
  xt_node_set(channel, "hasSubscribers", xt_closure_new((void *)channel_has_subscribers, 0, NULL));
  return channel;
}

xt_value xt_diagnostics_channel_static(xt_value name, int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  const char *fn = xt_string_data(name);
  if (fn && (strcmp(fn, "channel") == 0 || strcmp(fn, "tracingChannel") == 0)) {
    return diagnostics_channel_object();
  }
  return XT_UNDEFINED;
}
