
struct connection {

	sock_addr dst_addr, src_addr;
	GString *dst_addr_str, *src_addr_str;

	action_stack action_stack;

	request request;
	physical physical;

	GMutex *mutex;

	struct log_t *log;
	gint log_level;
};