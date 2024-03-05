#pragma once
//-------------------------------------------------------
// Queue of null-terminated strings
//
// Dean Gienger, Feb 29, 2024
//-------------------------------------------------------
class StrQueue
{
public:
	StrQueue(int nsize) { buf = new char[nsize+1]; maxsize = nsize; rdptr = wrptr = 0; }
	int isEmpty() { return wrptr == rdptr; }
	int push(char* s);
	int push(const char* s) { return push((char*)s); }
	int pop(char* buf, int maxlen);
	int used() { return (wrptr >= rdptr) ? wrptr - rdptr : maxsize + wrptr - rdptr; }
	void makeEmpty() { rdptr = wrptr = 0; }
	int available() { return maxsize - used() - 1; }
	int size() { return maxsize; }
private:
	char* buf;
	int wrptr;
	int rdptr;
	int maxsize;
};

