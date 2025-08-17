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

struct list_head {
	struct list_head *next, *prev;
};

void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

void list_cut_before(struct list_head *list,
		struct list_head *head,
		struct list_head *entry) {
	if (head->next == entry) {
		INIT_LIST_HEAD(list);
		return;
	}
	list->next = head->next;
	list->next->prev = list;
	list->prev = entry->prev;
	list->prev->next = list;
	head->next = entry;
	entry->prev = head;
}

struct list_head *list1;
struct list_head *list2;

void test_2() {
	INIT_LIST_HEAD(list1);
	INIT_LIST_HEAD(list2);	
	list_cut_before(list1, list2, list2);
}
int main() {
	file_t.x = 0;
	test();
	return 0;

}
