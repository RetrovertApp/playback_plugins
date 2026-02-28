#include <string.h>

int
main(void)
{
	const char *foo = "foo";
	if (strnlen(foo, 256) != 3)
		return 1;
	return 0;
}
