int a = 0;
struct meta{
	int m;
	int n;
};

struct file {
	int x;
	int y;
	struct meta *z;
};
int r[5] = {0,0,0,0,0};

struct file file_t;
int add(int x,int y) {
	return x+y;
}
int test() {
	int *b = &a;
	struct file *c = &file_t;
	int *p = r;
	p++;
	p = p + 2;
	*p = *b + 1;
	*b = 1;
	struct meta* d = c->z;
	d = (struct meta*) 0;
	c->y = 1;
	add(*b,a);
	return 0;
}

int main() {
	file_t.x = 0;
	test();
	return 0;

}
