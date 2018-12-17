#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <sys/resource.h>
#define MAGIC "FaSTdEvL"
#define DEBUG 1
#define BS 4096

/**
 * Stored in block 0 and its backup in block 1
 * 
 * magic: Magic string
 * label: FS label
 * blocksize: blocksize of the FS
 * blocks: Total number of blocks
 * n_inodes: Total number of inodes in the FS
 * inodes: Number of inodes per block
 * root: Root of the B+ Tree, initially -1
 * freeblocksmap: number of blocks for freeblocks map
 * idcounter: maintains a count of id# last assigned. Useful for item id generation
 * padding: Padding bytes
 */
struct superblock {
	char magic[8];
	char label[8];
	int blocksize;
	int blocks;
	int n_inodes;
	int inodes;
	int root;
	int freeblocksmap;
	int idcounter;
	char padding[4052];
};

/**
 * Each key is the unit of comparision in the B+ tree.
 * dir_id: id of the parent directory
 * id: id of the current file/directory
 */
struct Key {
	unsigned dir_id;
	unsigned id;
};

/**
 * n = degree of B+ tree. Then each leaf has a maximum of n children links.
 * And a maximum of n-1 keys in each node
 *  parent: location of parent of the node. -1, for root node
 * 	isLeaf: 1 if node is leaf, else 0
 * 	size: number of keys currently in the node
 *  key[339]: stores keys in the node
 *  link[340]: stores links to children of the node, but in case of leaf node, stores inode location
 * 	left: logical left node of leaf node
 * 	right: logical right node of leaf node
 *  padding: padding bytes to match size of structure with blocksize
 * if non-leaf node, left and right should be set to -1
 * (5 * 4 + 4 * n + 8 * (n - 1)) = 4096, because we want the size to match the block size.
 * => n = 340.67 = 340
 */
struct node {
	int parent;
	int isLeaf;
	int size;
	struct Key key[339];
	int link[340];
	int left;
	int right;
	char padding[4];
};

int comp_str(char *, char *, int);
void preorder(int, FILE *);

int main()
{
	FILE *p;
	struct superblock sb;
	char fname[256];

	printf("Enter partition file name: ");
	scanf("%255s", fname);

	if (access(fname, F_OK) != -1) {
		p = fopen(fname, "rb+");
	} else {
		printf("\nFile not found. Please provide a valid image file.");

		exit(1);
	}

	fseek(p, 0, SEEK_SET);
	fread(&sb, sizeof(struct superblock), 1, p);

	if (comp_str(sb.magic, MAGIC, 8) != 0) {
		printf("\n\tInvalid partition detected. Exiting.");

		exit(2);
	}

	if (sb.root == -1) {
		printf("\nEmpty filesystem. No B+ tree found.");

		return 0;
	}

	preorder(sb.root, p);

	return 0;
}

int comp_str(char a[], char b[], int len)
{
	int i;

	for (i = 0; i < len; ++i) {
		if (a[i] != b[i]) {
			return 1;
		}
	}

	return 0;
}

void preorder(int root, FILE *p)
{
	int i;
	struct node n;

	fseek(p, root, SEEK_SET);
	fread(&n, 4096, 1, p);

	printf("n.size=%d ", n.size);
	printf(" (");

	for (i = 0; i < n.size; ++i) {
		printf(" [%d, %d]", n.key[i].dir_id, n.key[i].id);
	}

	printf(" ) ");
	printf(", R: %d", n.right);

	if (n.isLeaf == 1) {
		printf("\n\n");

		return;
	}

	for (i = 0; i <= n.size; ++i) {
		printf(" child %d: ", i);
		preorder(n.link[i], p);
	}

	return;
}
