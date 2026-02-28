#include <limits.h>
#include <stdlib.h>

char *canonicalize_file_name(const char *path)
{
#if defined(__AMIGA__) || defined(__AROS__)
	// TODO
	char *s = strndup(path, PATH_MAX);
#else
	char *s = malloc(PATH_MAX);
	if (s == NULL)
		return NULL;

	if (realpath(path, s) == NULL) {
		free(s);
		return NULL;
	}
#endif
	return s;
}

