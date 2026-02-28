#include <string.h>

int
main(void)
{
	char *foo = "foo,bar";
	if (!strsep(&foo, ","))
		return 1;
	return 0;
}
