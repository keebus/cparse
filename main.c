#include "cparse.h"
#include <malloc.h>
#include <stdio.h>

int main()
{
	struct cparse_info info;
	info.buffer_size = 1024;
	info.buffer = realloc(0, info.buffer_size);

	struct cparse_unit* unit = NULL;
	enum cparse_result result;

repeat:
	result = cparse_file("sample.h", &info, &unit);

	if (result == CPARSE_RESULT_OK)
	{
		for (struct cparse_node_decl* decl = unit->decls; decl; decl = decl->next)
		{
			printf("declaration: %s\n", decl->spelling);
		}
	}
	else if (result == CPARSE_RESULT_OUT_OF_MEMORY)
	{
		info.buffer_size *= 2;
		info.buffer = realloc(info.buffer, info.buffer_size);
		goto repeat;
	}
	else {
		printf("%s\n", info.buffer);
	}

	free(info.buffer);
}

#define CPARSE_IMPLEMENTATION
#include "cparse.h"
