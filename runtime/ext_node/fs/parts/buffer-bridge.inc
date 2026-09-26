/* -- Buffer bridge (defined in buffer/parts/prototype.inc) ---------------- */

int xt_node_is_buffer(xt_value value);
unsigned char *xt_node_buffer_bytes(xt_value value, size_t *outLength);
uint32_t xt_node_buffer_length(xt_value value);
void xt_node_buffer_set(xt_value value, uint32_t index, unsigned char byte);
