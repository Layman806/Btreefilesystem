#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <sys/resource.h>
#define MAGIC "FaSTdEvL"
#define BS 4096
#define DEBUG 0

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
 *  64 bytes inode 
 *  Each inode can point to an item.
 *  An item can be either a stat item, a directory or a simple file.
 *  f[0]: points to the stat file, is -1 if unoccupied
 *  f[1-13]: point to direct blocks of the file
 *  f[14]: points to a block, which contains pointers to other blocks (single indirect)
 *  f[15]: points to indirect blocks (double indirect)
 */
struct inode {
	int f[16];
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
 * Stores item stats
 *
 * id: id of file/directory
 * inode: inode number of the file/directory
 * dir_id: id of directory it belongs to
 * type: 4 for file, 2 for directory
 * lastblock: location of last block of the file
 * lastblockbytes: number of bytes in last block of the file. Used when reading the file.
 * uid: Linux user id value; range [0-65534], where 65534 is reserved for nobody
 * gid: Linux group id value
 * name: file/directory name
 * ctime: creation time; 	Time format "Day DD/MM/YYYY HH:MM:SS" = 24 characters including NULL character
 * ltime: last access time
 * mtime: last modification time
 * perm: file permissions, rwxrwxrwx: 9 bits required; 3 Bytes char used
 * blocks: number of blocks in file
 * padding: padding bytes for matching structure size with blocksize
 */
struct stat {
	struct Key k;
	int inode;
	int type;
	int lastblock;
	int lastblockbytes;
	unsigned short uid;
	unsigned short gid;
	char name[256];
	char ctime[25];
	char ltime[25];
	char mtime[25];
	char perm[3];
	int blocks;
	char padding[3728];
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

struct freeblock {
	char f[4096];
};

bool mount(FILE **, char[]);
void makefs(FILE *);
void setlabel(FILE *, char[]);
void showinfo();
void remount(FILE **, char[]);
int comp_str(char[], char[], int len);
int init_freemap(FILE *, int blocks);
int get_node(FILE *, struct superblock *sb);
void use_block(FILE *, int i);
void free_block(FILE *, int i);
void insert(FILE *, int id, int dir_id, int block, struct superblock *);
int promote(struct Key k, int parent, int l, int r, FILE *p, struct superblock *sb);
int find_parent(struct Key, FILE *, struct superblock *);
void debug_show_filled_blocks(FILE *);
bool check_block(FILE *, int);
int get_free_block(FILE *, struct superblock *);
void update_sb(FILE *, struct superblock *);
int comparator(const void *, const void *);
void err_noblocks();
void init_inodes(FILE *, struct superblock *sb);
int get_inode(FILE *, struct superblock *sb);
int new_empty_file_dir(FILE *, struct superblock *, char *, int, int);
int get_id(char *, FILE *, struct superblock *);
void init_stat(struct stat *, struct Key, int inode_loc, int type, char *name);
void get_time(char *);
void ls(FILE *, struct superblock *, int);
void debug_showroot(FILE *, struct superblock *);
void batch_create_files(FILE *, struct superblock *, int n, int dir_id);
void inorder(FILE *, int);
int find(FILE *, struct superblock *, int, char *, int, int);
void import(FILE *, struct superblock *sb, char *path, int dir_id, char *name);
void extract(FILE *, struct superblock *, int dir_id, char *, char *);

int main()
{
	FILE *p;
	struct superblock sb;
	int tmp;
	int pwd_id;
	int new_id;
	char change[256];
	char pwd[256];
	char name[256];
	char fname[256];
	char choice[20];
	char label[8];
	char t[25];

	get_time(t);
	strcpy(pwd, "/");
	pwd_id = 1;

	printf("\nCurrent time: %s", t);
	printf("\nSize of superblock: %lu", sizeof(struct superblock));
	printf("\nSize of one inode: %lu", sizeof(struct inode));
	printf("\nSize of one stat file: %lu", sizeof(struct stat));
	printf("\nSize of one int: %lu", sizeof(int));
	printf("\nSize of B+ tree node: %lu", sizeof(struct node));
	printf("\nDegree of B+ tree: 340");
	printf("\n");
	strcpy(name, "part1.img");
/*	printf("\nEnter name of file in which psuedo partition is stored(without spaces): ");

	scanf("%255s", name);
*/
	if (mount(&p, name) == false) {
		printf("\nPartition mount failed. Maybe it is unformatted or file is corrupted.");
		printf("\nMaybe try creating a partition or recovery options.\n");
	}

	printf("\n\n>>");
	scanf("%s", choice);

	while (strcmp(choice, "quit") != 0) {
		if (strcmp(choice, "makefs") == 0) {
			printf("\nCreating new filesystem.");
			makefs(p);
			printf("\nDone.");
		} else if (strcmp(choice, "setlabel") == 0) {
			scanf("%s", label);
			setlabel(p, label);
			remount(&p, name);
		} else if ((strcmp(choice, "remount") == 0) || (strcmp(choice, "mount") == 0)) {
			remount(&p, name);
		} else if (strcmp(choice, "debug_show_filled_blocks") == 0) {
			debug_show_filled_blocks(p);
		} else if (strcmp(choice, "newfile") == 0) {
			if (p == NULL) {
				printf("\nPlease mount fs first.");
				continue;
			}
			scanf("%255s", fname);
			fseek(p, 0, SEEK_SET);
			fread(&sb, sizeof(struct superblock), 1, p);
			printf("\nFile ID: %d\n", get_id(fname, p, &sb));
			new_empty_file_dir(p, &sb, fname, pwd_id, 4);
		} else if (strcmp(choice, "ls") == 0) {
			fseek(p, 0, SEEK_SET);
			fread(&sb, sizeof(struct superblock), 1, p);
			ls(p, &sb, pwd_id);
		} else if (strcmp(choice, "pwd") == 0) {
			printf("%s\n", pwd);
		} else if (strcmp(choice, "debug_showroot") == 0){
			fseek(p, 0, SEEK_SET);
			fread(&sb, sizeof(struct superblock), 1, p);
			debug_showroot(p, &sb);
		} else if (strcmp(choice, "bcf") == 0) {
			scanf("%d", &tmp);
			fseek(p, 0, SEEK_SET);
			fread(&sb, sizeof(struct superblock), 1, p);
			batch_create_files(p, &sb, tmp, pwd_id);
		} else if(strcmp(choice, "debug_inorder") == 0) {
			fseek(p, 0, SEEK_SET);
			fread(&sb, sizeof(struct superblock), 1, p);
			inorder(p, sb.root);
		} else if (strcmp(choice, "mkdir") == 0) {
			scanf("%s", fname);
			fseek(p, 0, SEEK_SET);
			fread(&sb, sizeof(struct superblock), 1, p);
			new_empty_file_dir(p, &sb, fname, pwd_id, 2);
		} else if (strcmp(choice, "cd") == 0) {
			scanf("%s", change);
			fseek(p, 0, SEEK_SET);
			fread(&sb, sizeof(struct superblock), 1, p);
			new_id = find(p, &sb, pwd_id, change, 2, 0);
			if (new_id == -1) {
				printf("\nDirectory \"%s\" does not exist!", change);
			} else {
				pwd_id = new_id;
				strcpy(pwd, change);
				printf("Entered directory: %s", change);
			}
		} else if (strcmp(choice, "import") == 0) {
			char path[256];

			scanf("%s", path);
			scanf("%s", fname);
			fseek(p, 0, SEEK_SET);
			fread(&sb, 4096, 1, p);
			import(p, &sb, path, pwd_id, fname);
		} else if (strcmp(choice, "export") == 0) {
			char path[256];

			scanf("%s %s", fname, path);
			fseek(p, 0, SEEK_SET);
			fread(&sb, 4096, 1, p);
			extract(p, &sb, pwd_id, fname, path);
		} else if (strcmp(choice, "find") == 0) {
			scanf("%s", fname);
			fseek(p, 0, SEEK_SET);
			fread(&sb, 4096, 1, p);
			if (find(p, &sb, pwd_id, fname, 4, 0) != -1) {
				printf("\nFound file %s", fname);
			} else {
				printf("\nNo file by the name %s", fname);
			}
			if (find(p, &sb, pwd_id, fname, 2, 0) != -1) {
				printf("\nFound directory %s", fname);
			} else {
				printf("\nNo directory by the name %s", fname);
			}
		} else {
			printf("\nInvalid choice (Enter quit to exit)");
		}
		printf("\n>>");
		scanf("%s", choice);
	}

	fclose(p);

	return 0;
}

void showinfo(struct superblock sb)
{
	int size;
	int count;
	int tmp;
	char cat[3];

	count = 0;
	tmp = sb.blocksize;

	do {
		++count;
		tmp = tmp>>10;
	} while (tmp>>10 > 0);

	size = tmp;
	tmp = sb.blocks;

	do {
		++count;
		tmp = tmp>>10;
	} while (tmp>>10 > 0);

	size *= tmp;
	strcpy(cat, "B");

	switch (count) {
		case 0:	strcpy(cat, "B");
			break;
		case 1: cat[1] = 'K';
			strcpy(cat, "KB");
			break;
		case 2: cat[1] = 'M';
			strcpy(cat, "MB");
			break;
		case 3: cat[1] = 'G';
			strcpy(cat, "GB");
			break;
		case 4: cat[1] = 'T';
			strcpy(cat, "TB");
			break;
		case 5: strcpy(cat, "PB");
			break;
		default:
			break;
	};

	printf("\n----------------------------------");
	printf("\n\t Filesystem info: ");
	printf("\nLabel: %s", sb.label);
	printf("\nBlocksize: %d", sb.blocksize);
	printf("\nSize: %d %s", size, cat);
	printf("\nBlocks: %d", sb.blocks);
	printf("\nTotal Inodes: %d", sb.n_inodes);
	printf("\nInodes per block: %d", sb.inodes);
	printf("\n#Blocks reserved for freeblocks bitmap: %d", sb.freeblocksmap);
	if (sb.root == -1) {
		printf("\nNo files/directories in fs.");
	} else {
		printf("\nNon-empty fs.");
	}
	printf("\n----------------------------------\n");

	return;
}

bool mount(FILE **p, char name[])
{
	struct superblock sb;
	char ch;


	if (access(name, F_OK) != -1) {
		*p = fopen(name, "rb+");
	} else {
		printf("\nFile not found. Please provide a valid image file.");

		return false;
	}

	fseek(*p, 0, SEEK_SET);
	fread(&sb, sizeof(struct superblock), 1, *p);

	if (comp_str(sb.magic, MAGIC, 8) != 0) {
		printf("\n\tInvalid partition detected. Want to create new filesystem on partition? (Y/n) : ");
		scanf(" %c", &ch);

		if ((ch == 'y') || (ch == 'Y')) {
			makefs(*p);
		} else {

			return false;
		}

		fseek(*p, 0, SEEK_SET);
		fread(&sb, sizeof(struct superblock), 1, *p);
		fseek(*p, 0, SEEK_SET);

		if (comp_str(sb.magic, MAGIC, 8) != 0) {
			printf("\n\tMagic string read was: %s, Requires: %s", sb.magic, MAGIC);
			printf("\n\tCreating new filesystem failed! Mission Abort!");

			return false;
		} else {
			printf("\nCreated new filesystem.");
		}
	}

	printf("Mounting filesystem complete!");

	showinfo(sb);

	return true;
}

void makefs(FILE *p)
{
	long int size;
	struct superblock SuperB;

	fseek(p, 0, SEEK_END);
	size = ftell(p);
	fseek(p, 0, SEEK_SET);

	strcpy(SuperB.magic, MAGIC);
	strcpy(SuperB.label, "NEWLABEL");
	SuperB.blocksize = 4096;
	SuperB.blocks = size / 4096;
	SuperB.n_inodes = (SuperB.blocks) / 10;

	if ((SuperB.blocks % 10) != 0) {
		++SuperB.n_inodes;
	}

	SuperB.root = -1;
	SuperB.inodes = 64;
	SuperB.freeblocksmap = init_freemap(p, SuperB.blocks);
	SuperB.idcounter = 2;
	init_inodes(p, &SuperB);

	fseek(p, 0, SEEK_SET);
	fwrite(&SuperB, sizeof(struct superblock), 1, p);
	fseek(p, 4096, SEEK_SET);
	fwrite(&SuperB, sizeof(struct superblock), 1, p);
	fseek(p, 0, SEEK_SET);

	return;
}

void setlabel(FILE *p, char label[8])
{
	struct superblock sb;

	fseek(p, 0, SEEK_SET);
	fread(&sb, sizeof(struct superblock), 1, p);
	fseek(p, 0, SEEK_SET);

	strcpy(sb.label, label);

	fwrite(&sb, sizeof(struct superblock), 1, p);
	fwrite(&sb, sizeof(struct superblock), 1, p);

	return;
}

void remount(FILE **p, char name[])
{
	if (*p == NULL) {
		printf("\n\tERROR: Remount failed as NULL was passed to be remounted.");

		exit(1);
	}

	fclose(*p);
	*p = fopen(name, "rb+");
	mount(p, name);

	return;
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

/**
 * returns number of blocks reserved for freeblocks map
 */
int init_freemap(FILE *p, int blocks)
{
	struct freeblock *freemap;
	int freeblocks;
	int i;
	int j;

	freeblocks = blocks/(8 * 4096);

	if ((blocks % (8 * 4096)) != 0) {
		++freeblocks;
	}

	freemap = (struct freeblock *) malloc(freeblocks * sizeof(struct freeblock));
	i = 0;

	while (i < freeblocks) {
		j = 0;
		while (j < 4096) {
			freemap[i].f[j] = 0;
			++j;
		}
		++i;
	}

	fseek(p, 2 * 4096, SEEK_SET);
	fwrite(freemap, freeblocks * sizeof(struct freeblock), 1, p);

	i = 0;
	while (i < (freeblocks + 2)) {
		use_block(p, i);
		++i;
	}

	return freeblocks;
}

/**
 * Sets i'th block's entry in freemap.
 * One block holds 8 * 4096 bits.
 * For i'th block:
 * 	b: block number
 * 	m: byte within b'th block number
 * 	x: bit within m'th byte of b'th block number
 * 	loc: byte location to be read and written back to
 */
void use_block(FILE *p, int i)
{
	int b;
	int m;
	int x;
	int loc;
	char a;
	int tmp; 

	b = i / (8 * 4096) + 2;
	m = (i % (8 * 4096)) / 8;
	x = (i % (8 * 4096)) % 8;

	loc = b * 4096 + m;

	fseek(p, loc, SEEK_SET);
	fread(&a, sizeof(char), 1, p);

	a ^= (1 << x);

	fseek(p, loc, SEEK_SET);
	fwrite(&a, sizeof(char), 1, p);

	tmp = (a >> x) && 1;

	if (DEBUG)
		printf("\nToggled block %d, byte %d, index %d, byte location: %d, to %d", b, m, x, loc, tmp);

	return;
}

/**
 * use_block(FILE *, int) basically toggles bit using XOR operation, so, freeing a 
 * used block needs only toggling bit from 1 to 0, which is achieved by using the 
 * same function.
 * */
void free_block(FILE *p, int i)
{
	use_block(p, i);

	return;
}

int get_node(FILE *p, struct superblock *sb)
{
	int fb;
	int i;
	struct node nn;

	fb = get_free_block(p, sb);

	if (fb == -1) {
		return -1;
	}

	nn.parent = -1;
	nn.isLeaf = 0;
	nn.size = 0;
	nn.left = -1;
	nn.right = -1;

	for (i = 0; i < 339; ++i) {
		nn.key[i].dir_id = -1;
		nn.key[i].id = -1;
		nn.link[i] = -1;
	}

	nn.link[339] = -1;

	use_block(p, fb);

	fb *= 4096;

	fseek(p, fb, SEEK_SET);
	fwrite(&nn, sizeof(struct node), 1, p);

	return fb;
}

void insert(FILE *p, int id, int dir_id, int block, struct superblock *sb)
{
	bool flag;
	int curr;
	int i;
	int j;
	int l;
	int r;
	struct node n;
	struct node tmp1;
	struct node tmp2;
	struct Key k;

	k.dir_id = dir_id;
	k.id = id;

	if (sb->root == -1) {
		curr = get_node(p, sb);

		if (curr == -1) {
			err_noblocks();

			return;
		}

		sb->root = curr;
		fseek(p, curr, SEEK_SET);
		fread(&n, sizeof(struct node), 1, p);

		n.parent = -1;
		n.isLeaf = 1;
		n.size = 1;
		n.key[0].dir_id = dir_id;
		n.key[0].id = id;
		n.link[0] = block;
		n.left = -1;
		n.right = -1;

		fseek(p, curr, SEEK_SET);
		fwrite(&n, sizeof(struct node), 1, p);

		update_sb(p, sb);

		return;
	} else {
		fseek(p, sb->root, SEEK_SET);
		fread(&n, sizeof(struct node), 1, p);
		curr = sb->root;

		while (n.isLeaf != 1) {
			if (DEBUG) {
				printf("\n\tSearching for leaf...");
			}
			for (i = 0; i < n.size; ++i) {
				if (comparator((void *)&k, (void *)&n.key[i]) < 0) {
					curr = n.link[i];
					fseek(p, curr, SEEK_SET);
					fread(&n, sizeof(struct node), 1, p);
					break;
				}
			}
			if (i == n.size) {
				curr = n.link[i];
				fseek(p, curr, SEEK_SET);
				fread(&n, sizeof(struct node), 1, p);
			}
		}

		if (n.size < 339) {
			if (DEBUG)
				printf("\nInside first condition.");
			for (i = 0; i < n.size; ++i) {
				if (comparator((void *)&k, (void *)&n.key[i]) < 0) {
					break;
				}
			}

			for (j = n.size; j > i; --j) {
				n.key[j] = n.key[j-1];
				n.link[j] = n.link[j-1];
			}

			n.key[i] = k;
			n.link[i] = block;
			++n.size;

			if (DEBUG)
				printf("\nIncremented size to %d", n.size);

			fseek(p, curr, SEEK_SET);
			fwrite(&n, sizeof(struct node), 1, p);
		} else {
			l = get_node(p, sb);
			r = get_node(p, sb);

			if (l == -1 || r == -1) {
				err_noblocks();

				return;
			}

			fseek(p, l, SEEK_SET);
			fread(&tmp1, sizeof(struct node), 1, p);
			tmp1.left = n.left;
			tmp1.right = r;
			tmp1.parent = n.parent;
			tmp1.isLeaf = 1;

			fseek(p, r, SEEK_SET);
			fread(&tmp2, sizeof(struct node), 1, p);
			tmp2.right = n.right;
			tmp2.left = l;
			tmp2.parent = n.parent;
			tmp2.isLeaf = 1;

			flag = false;
			for (i = 0, j = 0; j < ((n.size / 2) + 1);) {
				if ((flag == false) && (comparator((void *)&k, (void *)&n.key[i]) < 0)) {
					tmp1.key[j] = k;
					tmp1.link[j] = block;
					++j;
					flag = true;
				} else {
					tmp1.key[j] = n.key[i];
					tmp1.link[j] = n.link[i];
					++i;
					++j;
				}
			}
			tmp1.size = j;

			for (j = 0; i < n.size;) {
				if ((flag == false) && (comparator((void *)&k, (void *)&n.key[i]) < 0)) {
					tmp2.key[j] = k;
					tmp2.link[j] = block;
					++j;
					flag = true;
				} else {
					tmp2.key[j] = n.key[i];
					tmp2.link[j] = n.link[i];
					++i;
					++j;
				}
			}
			if (flag == false) {
				tmp2.key[j] = k;
				tmp2.link[j] = block;
				++j;
				flag = true;
			}
			tmp2.parent = n.parent;
			tmp2.size = j;

			free_block(p, curr);

			if (DEBUG) {
				inorder(p, l);
				inorder(p, r);
			}

			tmp1.parent = promote(tmp2.key[0], n.parent, l, r, p, sb);
			tmp2.parent = tmp1.parent;

/*			tmp1.parent = find_parent(tmp1.key[0], p, sb);
			tmp2.parent = find_parent(tmp2.key[0], p, sb);
*/
			tmp2.left = l;
			tmp2.right = n.right;
			fseek(p, r, SEEK_SET);
			fwrite(&tmp2, sizeof(struct node), 1, p);

			tmp1.left = n.left;
			tmp1.right = r;
			fseek(p, l, SEEK_SET);
			fwrite(&tmp1, sizeof(struct node), 1, p);

			if (n.left != -1) {
				fseek(p, n.left, SEEK_SET);
				fread(&tmp1, BS, 1, p);
				tmp1.right = l;
				fseek(p, n.left, SEEK_SET);
				fwrite(&tmp1, BS, 1, p);
			}

			if (n.right != -1) {
				fseek(p, n.right, SEEK_SET);
				fread(&tmp2, BS, 1, p);
				tmp2.left = r;
				fseek(p, n.left, SEEK_SET);
				fwrite(&tmp2, BS, 1, p);
			}
		}
	}

	return;
}

void debug_show_filled_blocks(FILE *p)
{
	int i;
	struct superblock sb;

	if (!DEBUG) {
		printf("Sorry, binary compiled as production, not debug. Set the DEBUG flag in source and compile to access the feature.");
		return;
	}

	fseek(p, 0, SEEK_SET);
	fread(&sb, sizeof(struct superblock), 1, p);

	printf("\nBlocks in use: ");
	for (i = 0; i < sb.blocks; ++i) {
		if (check_block(p, i) == true) {
			printf("%d ", i);
		}
	}

	return;
}

/**
 * returns true if block i is in use, else false
 */
bool check_block(FILE *p, int i)
{
	int b;
	int m;
	int x;
	int loc;
	char a;

	b = i / (8 * 4096) + 2;
	m = (i % (8 * 4096)) / 8;
	x = (i % (8 * 4096)) % 8;

	loc = b * 4096 + m;

	fseek(p, loc, SEEK_SET);
	fread(&a, sizeof(char), 1, p);

	if ((a >> x) & 1) {
		return true;
	} else {
		return false;
	}
}

/**
 * This function can be optimized by reading block-wise, storing in struct freeblock, 
 * and then checking if each f is less than 255. If less than 255, at least one block
 * is unoccupied. Then check for unoccupied block within f.
 * */
int get_free_block(FILE *p, struct superblock *sb)
{
	int fb;

	for (fb = (2 + sb->freeblocksmap); fb < sb->blocks; ++fb) {
		if (check_block(p, fb) == false) {
			break;
		}
	}

	if (fb == sb->blocks) {
		err_noblocks();

		return -1;
	}

	return fb;
}

void update_sb(FILE *p, struct superblock *sb)
{
	fseek(p, 0, SEEK_SET);
	fwrite(sb, sizeof(struct superblock), 1, p);

	fseek(p, 4096, SEEK_SET);
	fwrite(sb, sizeof(struct superblock), 1, p);

	return;
}

int comparator(const void *p, const void *q)
{
	if ((((struct Key *)p)->dir_id - ((struct Key *)q)->dir_id) == 0) {
		return (((struct Key *)p)->id - ((struct Key *)q)->id);
	} else {
		return (((struct Key *)p)->dir_id - ((struct Key *)q)->dir_id);
	}
}

int promote(struct Key k, int parent, int l, int r, FILE *p, struct superblock *sb)
{
	int i;
	int j;
	struct node n;

	if (parent == -1) {
		if (DEBUG) {
			printf("\n\nParent is -1, so creating new root.\n");
		}

		parent = get_node(p, sb);

		if (parent == -1) {
			err_noblocks();

			return parent;
		}

		fseek(p, parent, SEEK_SET);
		fread(&n, sizeof(struct node), 1, p);

		n.parent = -1;
		n.isLeaf = 0;
		n.key[0] = k;
		n.link[0] = l;
		n.link[1] = r;
		n.left = -1;
		n.right = -1;
		n.size = 1;

		fseek(p, parent, SEEK_SET);
		fwrite(&n, sizeof(struct node), 1, p);

		sb->root = parent;
		update_sb(p, sb);

		if (DEBUG) {
			inorder(p, parent);
		}

		return parent;
	}

	fseek(p, parent, SEEK_SET);
	fread(&n, sizeof(struct node), 1, p);


	if (n.size < 339) {
		for (i = 0; i < n.size; ++i) {
			if (comparator((void *)&k, (void *)&n.key[i]) < 0) {
				break;
			}
		}

		for (j = n.size; j > i; --j) {
			n.key[j] = n.key[j-1];
			n.link[j+1] = n.link[j];
		}

		n.key[i] = k;
		n.link[i] = l;
		n.link[i + 1] = r;
		++n.size;

		fseek(p, parent, SEEK_SET);
		fwrite(&n, sizeof(struct node), 1, p);

		return parent;
	} else {
		bool flag;
		int L;
		int R;
		struct node tmp1;
		struct node tmp2;
		struct Key ktmp;

		L = get_node(p, sb);
		R = get_node(p, sb);

		if ((L == -1) || (R == -1)) {
			err_noblocks();

			return parent;
		}

		flag = false;

		fseek(p, L, SEEK_SET);
		fread(&tmp1, sizeof(struct node), 1, p);

		fseek(p, R, SEEK_SET);
		fread(&tmp2, sizeof(struct node), 1, p);

		for (i = 0, j = 0; i < ((n.size / 2) + 1);) {
			if ((flag == false) && (comparator((void *)&k, (void *)&n.key[j]) < 0)) {
				tmp1.key[i] = k;
				tmp1.link[i] = l;
				tmp1.link[i + 1] = r;
				n.link[j] = r;
				parent = L;
				flag = true;
				++i;
			} else {
				tmp1.key[i] = n.key[j];
				tmp1.link[i] = n.link[j];
				++i;
				++j;
			}
		}
		if (flag == false) {
			tmp1.link[i] = n.link[j];
		}

		if ((flag == false) && (comparator((void *)&k, (void *)&n.key[j]) < 0)) {
			ktmp = k;
			tmp1.link[i] = l;
			tmp2.link[0] = r;
			tmp2.key[0] = n.key[j];
			++j;
			i = 1;
			parent = -1;
			flag = true;
		} else {
			i = 0;
		}

		for (; j < n.size;) {
			if ((flag == false) && (comparator((void *)&k, (void *)&n.key[j]) < 0)) {
				tmp2.key[i] = k;
				tmp2.link[i] = l;
				tmp2.link[i + 1] = r;
				n.link[j] = r;
				++i;
				parent = R;
				flag = true;
			} else {
				tmp2.key[i] = n.key[j];
				tmp2.link[i] = n.link[j];
				++i;
				++j;
			}
		}
		if (flag == false) {
			tmp2.link[i] = n.link[j];
		}

		tmp1.parent = n.parent;

		free_block(p, parent);

		tmp1.parent = promote(ktmp, tmp1.parent, L, R, p, sb);
		tmp2.parent = tmp1.parent;

		if (tmp1.parent == -1) {
/*			tmp1.parent = find_parent(tmp1.key[0], p, sb);
			tmp2.parent = find_parent(tmp2.key[0], p, sb);
*/		}


		fseek(p, L, SEEK_SET);
		fwrite(&tmp1, sizeof(struct node), 1, p);
		fseek(p, R, SEEK_SET);
		fwrite(&tmp2, sizeof(struct node), 1, p);

		return parent;
	}
}

void err_noblocks()
{
	printf("\nERROR: No more free blocks in fs!");

	return;
}

int find_parent(struct Key k, FILE *p, struct superblock *sb)
{
	int prev;
	int curr;
	int i;
	struct node n;

	curr = sb->root;
	prev = -1;

	while (1) {
		fseek(p, curr, SEEK_SET);
		fread(&n, sizeof(struct node), 1, p);

		for (i = 0; i < n.size; ++i) {
			if (comparator((void *)&k, (void *)&n.key[i]) == 0) {
				return prev;
			}
		}

		prev = curr;

		for (i = 0; i < n.size; ++i) {
			if (comparator((void *)&k, (void *)&n.key[i]) < 0) {
				curr = n.link[i];
				break;
			}
		}
		if (i == n.size) {
			curr = n.link[i];
		}
	}

	return prev;
}

void init_inodes(FILE *p, struct superblock *sb)
{
	int i;
	int inode_blocks;
	int start;
	int end;
	struct inode in[64];

	for (i = 0; i < 64; ++i) {
		in[i].f[0] = -1;
	}

	inode_blocks = sb->n_inodes / sb->inodes;
	start = 2 + sb->freeblocksmap;
	end = start + inode_blocks;

	for (i = start; i < end; ++i) {
		fseek(p, 4096 * i, SEEK_SET);
		fwrite(&in, 64 * sizeof(struct inode), 1, p);
		use_block(p, i);
	}

	return;
}

/**
 * Returns byte location of first free inode found inside the allocated space for inodes.
 * */
int get_inode(FILE *p, struct superblock *sb)
{
	int i;
	int start;
	struct inode in;

	if (sb->n_inodes == 0) {
		printf("\nn_inodes is zero!");

		exit(0);
	}

	start = (2 + sb->freeblocksmap) * 4096;
	fseek(p, start, SEEK_SET);

	for (i = 0; i < sb->n_inodes; ++i) {
		fseek(p, (start + sizeof(struct inode) * i), SEEK_SET);
		fread(&in, sizeof(struct inode), 1, p);
		if (in.f[0] == -1) {
			return (start + i * sizeof(struct inode));
		}
	}

	return -1;
}

int new_empty_file_dir(FILE *p, struct superblock *sb, char name[], int dir_id, int type)
{
	int inode_loc;
	int stat_loc;
	struct inode in;
	struct Key k;
	struct stat s;

	if (find(p, sb, dir_id, name, type, 0) != -1) {
		if (type == 2) {
			printf("\nDirectory \"%s\" already exists!", name);
		} else if (type == 4) {
			printf("\nFile \"%s\" already exists!", name);
		} else {
			printf("\nItem \"%s\" already exists!", name);
		}

		return -2;
	}

	k.id = get_id(name, p, sb);
	k.dir_id = dir_id;

	inode_loc = get_inode(p, sb);
	stat_loc = get_free_block(p, sb);

	if (stat_loc == -1) {
		err_noblocks();

		return -1;
	}

	use_block(p, stat_loc);

	stat_loc *= 4096;

	if (DEBUG)
		printf("\nStat Loc: %d, inode loc: %d", stat_loc, inode_loc);

	in.f[0] = stat_loc;
	in.f[1] = -1;

	init_stat(&s, k, inode_loc, type, name);

	fseek(p, inode_loc, SEEK_SET);
	fwrite(&in, sizeof(struct inode), 1, p);

	fseek(p, stat_loc, SEEK_SET);
	fwrite(&s, sizeof(struct stat), 1, p);

	if (DEBUG) {
		printf("\nAbout to insert key.");
	}

	insert(p, k.id, k.dir_id, inode_loc, sb);

	if (type == 2) {
		k.dir_id = k.id;
		k.id = dir_id;

		inode_loc = get_inode(p, sb);
		stat_loc = get_free_block(p, sb);

		if (stat_loc == -1) {
			err_noblocks();

			return -1;
		}

		use_block(p, stat_loc);

		stat_loc *= 4096;

		if (DEBUG)
			printf("\nStat Loc: %d, inode loc: %d", stat_loc, inode_loc);

		in.f[0] = stat_loc;
		in.f[1] = -1;

		init_stat(&s, k, inode_loc, type, "..");

		fseek(p, inode_loc, SEEK_SET);
		fwrite(&in, sizeof(struct inode), 1, p);

		fseek(p, stat_loc, SEEK_SET);
		fwrite(&s, sizeof(struct stat), 1, p);

		if (DEBUG) {
			printf("\nAbout to insert key.");
		}

		insert(p, k.id, k.dir_id, inode_loc, sb);
	}

	fseek(p, 0, SEEK_SET);
	fwrite(sb, sizeof(struct superblock), 1, p);

	return inode_loc;
}

void ls(FILE *p, struct superblock *sb, int dir_id)
{
	int i;
	int curr;
	struct node n;
	struct inode in;
	struct stat s;

	curr = sb->root;

	if (curr == -1) {
		return;
	}
 
	fseek(p, sb->root, SEEK_SET);
	fread(&n, sizeof(struct node), 1, p);
	if (DEBUG) {
		printf("\n%d is first dir_id of root, n.size = %d", n.key[0].dir_id, n.size);
	}

	while (n.isLeaf == 0) {
		for (i = 0; i < n.size; ++i) {
			if (dir_id < n.key[i].dir_id) {
				curr = n.link[i];
				break;
			}
		}
		if (i == n.size) {
			curr = n.link[i];
		}

		fseek(p, curr, SEEK_SET);
		fread(&n, sizeof(struct node), 1, p);
	}
	if (DEBUG) {
		printf("\n%d is first dir_id of first leaf", n.key[0].dir_id);
	}

	while (n.key[0].dir_id == dir_id) {
		if (n.left == -1) {
			break;
		}
		curr = n.left;
		fseek(p, curr, SEEK_SET);
		fread(&n, sizeof(struct node), 1, p);
	}

	while (1) {
		if (DEBUG) {
			printf("\ndir_id: %d, n.size = %d, n.right = %d\n", n.key[0].dir_id, n.size, n.right);
		}

		for (i = 0; i < n.size; ++i) {
			if (n.key[i].dir_id == dir_id) {
				fseek(p, n.link[i], SEEK_SET);
				fread(&in, sizeof(struct inode), 1, p);
				fseek(p, in.f[0], SEEK_SET);
				fread(&s, sizeof(struct stat), 1, p);
				if (s.type == 4) {
					printf("f ");
				} else {
					printf("D ");
				}
				printf("%20s    %25s\n", s.name, s.ltime);
			}
		}

		if (n.right == -1) {
			break;
		}

		fseek(p, n.right, SEEK_SET);
		fread(&n, sizeof(struct node), 1, p);

		if (n.key[0].dir_id != dir_id){
			break;
		}

/*		if (n.key[n.size - 1].dir_id == dir_id) {
			if (n.right == -1) {
				break;
			}
			fseek(p, n.right, SEEK_SET);
			fread(&n, sizeof(struct node), 1, p);
		} else {
			break;
		}
*/	}

	return;
}

void init_stat(struct stat *s, struct Key k, int inode_loc, int type, char *name)
{
	char t[25];

	get_time(t);

	if (DEBUG) {
		printf("\nGot time.");
	}

	s->k = k;
	s->inode = inode_loc;
	s->type = type;
	s->uid = 1000;
	s->gid = 100;
	strcpy(s->name, name);

	if (DEBUG) {
		printf("\nCopied name.");
	}

	strcpy(s->ctime, t);
	strcpy(s->ltime, t);
	strcpy(s->mtime, t);

	s->perm[0] = 7;
	s->perm[1] = 5;
	s->perm[2] = 5;

	if (DEBUG) {
		printf("\nStats generated successfully.");
	}

	return;
}

void get_time(char *t)
{
	time_t currtime;
	struct tm *loc_time;

	currtime = time(NULL);
	loc_time = localtime(&currtime);
	strcpy(t, asctime(loc_time));
	t[24] = '\0';

	return;
}

int get_id(char *name, FILE *p, struct superblock *sb)
{
	int id;

	if (strcmp(name, "/") == 0) {
		return 1;
	}

	id = sb->idcounter;

	++sb->idcounter;

	update_sb(p, sb);

	return id;
}

void debug_showroot(FILE *p, struct superblock *sb)
{
	int i;
	struct node n;

	if (!DEBUG) {
		printf("\n\tNOT compiled as debug.\n");

		return;
	}

	fseek(p, sb->root, SEEK_SET);
	fread(&n, sizeof(struct node), 1, p);

	printf("\nRoot location: %d. Root node contents: ", sb->root);

	for (i = 0; i < n.size; ++i) {
		printf("\ndir_id = %d, id = %d", n.key[i].dir_id, n.key[i].id);
		printf(", link: %d", n.link[i]);
	}

	return;
}

void batch_create_files(FILE *p, struct superblock *sb, int n, int dir_id)
{
	int i;
	int tmp;
	char fname[256];

	for (i = 0; i < n; ++i) {
		sprintf(fname, "batch_file_%d", i);
		tmp = new_empty_file_dir(p, sb, fname, dir_id, 4);
		if (tmp == -2) {
			++n;
		}
	}

	return;
}

void inorder(FILE *p, int root)
{
	int i;
	struct node n;

	if (!DEBUG) {
		printf("\nNOT in debug mode!");

		return;
	}

	if (root == -1) {
		return;
	}

	fseek(p, root, SEEK_SET);
	fread(&n, sizeof(struct node), 1, p);

	if (n.isLeaf == 1) {
		for (i = 0; i < n.size; ++i) {
			printf("%d ", n.key[i].id);
		}
	} else {
		for (i = 0; i < n.size; ++i) {
			inorder(p, n.link[i]);
		}
		inorder(p, n.link[i]);
	}

	return;
}

int find(FILE *p, struct superblock *sb, int dir_id, char name[], int type, int id_or_loc)
{
	int i;
	int curr;
	struct node n;
	struct inode in;
	struct stat s;

	curr = sb->root;

	if (curr == -1) {
		return -1;
	}

	fseek(p, sb->root, SEEK_SET);
	fread(&n, sizeof(struct node), 1, p);
	if (DEBUG) {
		printf("\n%d is first dir_id of root, n.size = %d", n.key[0].dir_id, n.size);
	}

	while (n.isLeaf == 0) {
		for (i = 0; i < n.size; ++i) {
			if (dir_id < n.key[i].dir_id) {
				if (DEBUG) {
					printf("\n\tTaking a left before dir_id: %d, and key #%d", n.key[i].dir_id, i);
				}
				curr = n.link[i];
				break;
			}
		}
		if (i == n.size) {
			if (DEBUG) {
				printf("\n\tTaking a right after dir_id: %d, and key #%d", n.key[i-1].dir_id, i);
			}
			curr = n.link[i];
		}

		fseek(p, curr, SEEK_SET);
		fread(&n, sizeof(struct node), 1, p);
	}
	if (DEBUG) {
		printf("\n%d is first dir_id of first leaf found", n.key[0].dir_id);
	}

	while (n.key[0].dir_id == dir_id) {
		if (n.left == -1) {
			break;
		}
		if (DEBUG) {
			printf("\n\tGoing left from this node because left node also has same dir_id");
		}
		curr = n.left;
		fseek(p, curr, SEEK_SET);
		fread(&n, sizeof(struct node), 1, p);
	}

	while (1) {
		if (DEBUG) {
			printf("\ndir_id: %d, n.size = %d, n.right = %d\n", n.key[0].dir_id, n.size, n.right);
		}
		for (i = 0; i < n.size; ++i) {
			if (n.key[i].dir_id == dir_id) {
				fseek(p, n.link[i], SEEK_SET);
				fread(&in, sizeof(struct inode), 1, p);
				fseek(p, in.f[0], SEEK_SET);
				fread(&s, sizeof(struct stat), 1, p);
				if (strcmp(s.name, name) == 0 && s.type == type) {
					if (s.type == type) {
						if (DEBUG) {
							printf("\nFound key at key index: %d", i);
						}
						if (id_or_loc == 0) {
							return s.k.id;
						} else {
							return n.link[i];
						}
					} else {
						printf("\n%s is not a ", name);

						if (type == 2) {
							printf("directory!");
						} else if (type == 4) {
							printf("file!");
						} else {
							printf("item!");
						}

						return -1;
					}
				}
			}
		}

		if (n.right == -1) {
			break;
		}

		if (n.key[n.size - 1].dir_id == dir_id) {
			if (n.right == -1) {
				break;
			}
			if (DEBUG) {
				printf("\n\tGoing right from this node because it also has the same dir_id");
			}
			fseek(p, n.right, SEEK_SET);
			fread(&n, sizeof(struct node), 1, p);
		} else {
			break;
		}
	}

	return -1;
}

void import(FILE *p, struct superblock *sb, char path[], int dir_id, char name[])
{
	FILE *f;
	int i;
	int j;
	int size;
	int inode_loc;
	int freeblock;
	int lastblock;
	int lastblockbytes;
	int blocks_req;
	int count;
	struct inode in;
	struct stat s;
	char block[4096];
	int d_indirect[1024];
	int indirect[1024];

	memset(block, 0, 4096);
	count = 0;

	if (access(path, F_OK) != 1) {
		f = fopen(path, "rb");
	} else {
		return;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	blocks_req = size / 4096;
	if (size % 4096 != 0) {
		++blocks_req;
		lastblockbytes = size % 4096;
	} else {
		lastblockbytes = 4096;
	}
	inode_loc = new_empty_file_dir(p, sb, name, dir_id, 4);

	printf("\nBlock size for reading file: %lu", sizeof(block));

	fseek(p, inode_loc, SEEK_SET);
	fread(&in, sizeof(struct inode), 1, p);

	for (i = 1; i < 16; ++i) {
		in.f[i] = -1;
	}

	fseek(f, 0, SEEK_SET);
	for (i = 1, count = 0; (i < 14) && (count < blocks_req); ++i) {
		printf("\n\n\tDirect block #%d", i);
		freeblock = get_free_block(p, sb);
		use_block(p, freeblock);
		freeblock *= 4096;

		in.f[i] = freeblock;
		in.f[i + 1] = -1;
		++count;
		lastblock = freeblock;
		fread(block, 4096, 1, f);
		fseek(p, freeblock, SEEK_SET);
		fwrite(block, 4096, 1, p);
	}

	if (count < blocks_req) {
		for (i = 0; i < 1024; ++i) {
			indirect[i] = -1;
		}

		for (i = 0; (i < 1024) && (count < blocks_req); ++i) {
			freeblock = get_free_block(p, sb);
			use_block(p, freeblock);
			freeblock *= 4096;
			indirect[i] = freeblock;
			++count;

			fseek(p, freeblock, SEEK_SET);
			lastblock = freeblock;
			fread(block, 4096, 1, f);
			fwrite(block, 4096, 1, p);
		}

		freeblock = get_free_block(p, sb);
		use_block(p, freeblock);
		freeblock *= 4096;
		in.f[14] = freeblock;

		fseek(p, freeblock, SEEK_SET);
		fwrite(indirect, 4096, 1, p);
	}

	if (count < blocks_req) {
		for (i = 0; i < 1024; ++i) {
			d_indirect[i] = -1;
		}

		for (i = 0; (i < 1024) && (count < blocks_req); ++i) {
			for (j = 0; j < 1024; ++j) {
				indirect[j] = -1;
			}

			for(j = 0; (j < 1024) && (count < blocks_req); ++j) {
				freeblock = get_free_block(p, sb);
				use_block(p, freeblock);
				freeblock *= 4096;
				indirect[j] = freeblock;
				++count;

				fseek(p, freeblock, SEEK_SET);
				lastblock = freeblock;
				fread(block, 4096, 1, f);
				fwrite(block, 4096, 1, p);
			}
			freeblock = get_free_block(p, sb);
			use_block(p, freeblock);
			freeblock *= 4096;
			d_indirect[i] = freeblock;

			fseek(p, freeblock, SEEK_SET);
			fwrite(indirect, 4096, 1, p);
		}
		freeblock = get_free_block(p, sb);
		use_block(p, freeblock);
		freeblock *= 4096;
		in.f[15] = freeblock;

		fseek(p, freeblock, SEEK_SET);
		fwrite(d_indirect, 4096, 1, p);
	}

	fclose(f);
	
	fseek(p, in.f[0], SEEK_SET);
	fread(&s, 4096, 1, p);
	s.lastblock = lastblock;
	s.lastblockbytes = lastblockbytes;
	s.blocks = blocks_req;
	fseek(p, in.f[0], SEEK_SET);
	fwrite(&s, 4096, 1, p);

	fseek(p, inode_loc, SEEK_SET);
	fwrite(&in, sizeof(struct inode), 1, p);

	if (DEBUG) {
		printf("\nLast block: %d, last block bytes: %d, blocks: %d", lastblock, s.lastblockbytes, s.blocks);
	}

	printf("\nWrote one file successfully. File size = %d Bytes", size);

	return;
}

void extract(FILE *p, struct superblock *sb, int dir_id, char *name, char *fname)
{
	FILE *f;
	int i;
	int j;
	int inode_loc;
	int lb;
	int lbb;
	int count;
	int blocks;
	char block[4096];
	struct inode in;
	struct stat s;

	if (DEBUG) {
		printf("\nBefore declaration of variables");
	}

	if (DEBUG) {
		printf("\nBefore declaring stat structure variable");
	}

	if (DEBUG) {
		printf("\nDeclared all variables for extraction");
	}

	inode_loc = find(p, sb, dir_id, name, 4, 1);

	if (inode_loc == -1) {
		return;
	}
/*
	if (access(fname, F_OK) != 1) {
		printf("\nFile with name %s already exists in local folder. Please choose another name.", fname);

		return;
	}
*/
	fseek(p, inode_loc, SEEK_SET);
	fread(&in, sizeof(struct inode), 1, p);

	fseek(p, in.f[0], SEEK_SET);
	fread(&s, sizeof(struct stat), 1, p);
	lb = s.lastblock;
	lbb = s.lastblockbytes;
	count = 0;
	blocks = s.blocks;

	f = fopen(fname, "wb");
	fseek(f, 0, SEEK_SET);
	i = 1;

	for (i = 1; (i < 14) && (in.f[i] != -1) && (count < blocks); ++i) {
		printf("\nReading direct block #%d", i);
		fseek(p, in.f[i], SEEK_SET);
		if (in.f[i] == lb) {
			fread(block, 4096, 1, p);
			fwrite(block, lbb, 1, f);
			++count;
			fclose(f);

			return;
		} else {
			fread(block, 4096, 1, p);
			fwrite(block, 4096, 1, f);
			++count;
		}
	}

	if ((in.f[14] != -1) && (count < blocks)) {
		int indirect[1024];

		fseek(p, in.f[14], SEEK_SET);
		fread(indirect, 4096, 1, p);

		for (i = 0; (i < 1024) && (indirect[i] != -1) && (count < blocks); ++i) {
			if (DEBUG) {
				printf("\n  Reading indirect block #%d", i);
			}
			fseek(p, indirect[i], SEEK_SET);
			if (indirect[i] == lb) {
				fread(block, 4096, 1, p);
				fwrite(block, lbb, 1, f);
				++count;
				fclose(f);

				return;
			} else {
				fread(block, 4096, 1, p);
				fwrite(block, 4096, 1, f);
				++count;
			}
		}
	}

	if ((in.f[15] != -1) && (count < blocks)) {
		int d_indirect[1024];

		fseek(p, in.f[15], SEEK_SET);
		fread(d_indirect, 4096, 1, p);

		for (i = 0; (i < 1024) && (d_indirect[i] != -1) && (count < blocks); ++i) {
			int indirect[1024];
			if (DEBUG) {
				printf("\n    Reading double indirect #%d", i);
			}
			fseek(p, d_indirect[i], SEEK_SET);
			fread(indirect, 4096, 1, p);

			for (j = 0; (j < 1024) && (indirect[j] != -1) && (count < blocks); ++j) {
				if (DEBUG) {
					printf("\n      Reading indirect block #%d", j);
				}
				fseek(p, indirect[j], SEEK_SET);
				if (indirect[j] == lb) {
					fread(block, 4096, 1, p);
					fwrite(block, lbb, 1, f);
					++count;
					fclose(f);

					return;
				} else {
					fread(block, 4096, 1, p);
					fwrite(block, 4096, 1, f);
					++count;
				}
			}
		}
	}
	fclose(f);

	return;
}
