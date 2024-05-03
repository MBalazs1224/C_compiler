int printf(const char* s, ...);

struct dog
{
	int a;
	int b;
	int* c;
};

int special(int a, int b)
{
	return a + b;
}
int main()
{
	printf("%i\n", special(10,10));
}