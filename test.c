int printf(const char* s, ...);

struct dog
{
	int a;
	int b;
	int* c;
};

struct dog set_dog()
{
	struct dog a;
	*a.c = 50;
	return a;
}
int main()
{
	struct dog d; set_dog();
	return *d.c;
}