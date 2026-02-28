#include <string.h>

int
main(void)
{
	const char *foo = "BAR:foo:bar";
	if (!strcasestr(foo,"FOO:"))
		return 1;
	return 0;
}
