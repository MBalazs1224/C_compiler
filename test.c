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
	int res;
	res = 0;
	if(1)
	{
		switch (res)
		{
			case 0:
				switch (5)
				{
					case 4:
						res = -5;
						break;
					case 5:
						res = res + 2;
						break;
				}
				break;
		}
		switch (4)
		{
			default:
				res = res + res;
		}
	}
	return res;
}