int printf(const char* s);


struct valami
{
    int a;
    int b;
};

struct valami func()
{
    struct valami asd;
    asd.a = 50;
    return asd;
}

int main()
{
    struct valami ret = func();
    return ret.a;
}