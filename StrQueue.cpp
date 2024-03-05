//-------------------------------------------------------
// Queue of null-terminated strings
//
// Dean Gienger, Feb 29, 2024
//-------------------------------------------------------
#include "StrQueue.h"
#include <string.h>

int StrQueue::push(char* s)
{
	// see if there's room
	int n = (int)strlen(s)+1;
	if (n > available()) return 0; // error
	for (int i = 0; i < n; i++)
	{
		buf[wrptr++] = s[i];
		if (wrptr >= maxsize) wrptr = 0; // wrap write pointer
	}
	return 1;
}

int StrQueue::pop(char* s, int maxlen)
{
	if (maxlen < 1) return 0;
	*s = '\0'; // init to empty string
	if (isEmpty()) return 0; // error - queue is empty

	for (int i = 0; i < (maxlen-1); i++)
	{
		char c = buf[rdptr++];
		if (rdptr >= maxsize) rdptr = 0; // wrap read pointer
		s[i] = c;
		if (c == '\0') break;
		if (rdptr == wrptr)
		{
			s[i + 1] = '\0';
			break;
		}
		if (i == (maxlen - 2))
		{
			s[i + 1] = '\0';
			break;
		}
	}
	return 1;
}
