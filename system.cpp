#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define MAX_WRITE 1024 /*写入文字可达1k字节*/
#define MEM_D_SIZE 100*1024*1024 /*100M磁盘空间*/
#define MSD 34 /*最大子目录数 34*/
#define MOFN 10 /*最大文件打开数 10*/
/*分配1k用于引导块*/
struct ext2_boot_block {
	/*必须使用相对地址*/
	int address_ext2_super_block;/*超级块的起始地址*/
	int address_ext2_group_desc;/*组苗描述符盘块起始地址*/
	int address_ext2_data_block_bitmap;/*数据盘块位图起始地址*/
	int address_ext2_inode_bitmap;/*索引节点盘块位图起始地址*/
	int address_ext2_inode_table;/*索引表起始地址*/
	int address_ext_root;/*根目录的起始地址*/
};
/*分配1k用于做超级块,保留全局信息*/
struct ext2_super_block {
	int s_blocks_count;/*盘块的总数*/
	int s_free_blocks_count;/*空闲盘块数*/
	int s_free_inodes_count;/*空闲索引节点数*/
	int s_first_data_block;/* 第一个数据盘块号*/
	int s_log_block_size;/*盘块大小*/
	int s_msd;/*最大子目录数*/
	int s_mofn; /*最大文件打开数*/
	int s_root_disk_no;/*根目录起始盘快号*/
	int s_root_disk_size;/*根目录大小*/
	int s_max_size;/*写入文字长度*/
	int s_dir_length; /*路径长度*/
};
/*分配1k空间用于存储组块描述符*/
struct ext2_group_desc {
	int bg_block_bitmap;/*数据盘快位图所在的盘块号*/
	int bg_inode_bitmap;/*索引节点位图所在的盘块号*/
	int bg_inode_table;/*索引节点表的起点所在的盘块号*/
	int bg_inode_table_size;/*索引表大小*/
};
/*分配1k的空间用于存储数据盘块位图*/
struct ext2_data_block_bitmap {
	int first_block;/*数据盘块的起始块号*/
	int use_blocks;/*正在使用的数据盘块*/
	int free_blocks;/*空闲的数据盘块*/
};
/*分配1k的空间用于存储索引节点盘块位图*/
struct ext2_inode_bitmap {
	int first_inode;/*索引节点起始号*/
	int use_inodes;/*正在使用的索引节点*/
	int free_inodes;/*空闲的索引节点*/
};
/*---------------索引节点结构-----------------------*/
struct ext2_inode /* size 8*/
{
	int i_block;/*指向一个盘块*/
	char em_disk; /*磁盘块是否空闲标志位 0 空闲0，非空闲1*/
};
/*-------------------目录项结构------------------------*/
struct ext2_dir_entry /* size 336*/
{
	/*-----文件控制快信息-----*/
	struct FCB
	{
		char name[9]; /*文件/目录名 8位*/
		char property; /*属性 1位目录 0位普通文件*/
		int size; /*文件/目录字节数(原注释位盘块数)*/
		int firstdisk; /*文件/目录 起始盘块号*/
		int next; /*子目录起始盘块号*/
		int sign; /*1是根目录 0不是根目录*/
	}directitem[MSD + 2];
};
/*------------------文件打开表项结构--------------------------*/
struct opentable /* size 204*/
{
	struct openttableitem /* size 20*/
	{
		char name[9]; /*文件名*/
		int firstdisk; /*起始盘块号*/
		int size; /*文件的大小*/
	}openitem[MOFN];
	int cur_size; /*当前打文件的数目*/
};
/*-------------------------------------------------------------------*/
struct ext2_boot_block *boot_block;
struct ext2_super_block *super_block;
struct ext2_group_desc *group_desc;
struct ext2_data_block_bitmap*data_block_bitmap;
struct ext2_inode_bitmap*inode_bitmap;
struct ext2_inode *inode; /*EXT2索引表*/
struct ext2_dir_entry *root; /*根目录*/
struct ext2_dir_entry *cur_dir; /*当前目录*/
struct opentable u_opentable; /*文件打开表*/
int fd = -1; /*文件打开表的序号*/
char *bufferdir; /*记录当前路径的名称*/
char *fdisk; /*虚拟磁盘起始地址*/
void initfile();
void format();
void enter();
void halt();
int create(char *name);
int open(char *name);
int close(char *name);
int write(int fd, char *buf, int len);
int read(int fd, char *buf);
int del(char *name);
int mkdir(char *name);
int rmdir(char *name);
void dir();
int cd(char *name);
void help();
void show();
/*------------------------------------------初始化文件系统
--------------------------------------*/
void initfile()
{
	fdisk = (char *)malloc(MEM_D_SIZE*sizeof(char)); /*申请 100M空间*/
	format();
	free(fdisk);
}
/*----------------------------------------------------------------------------------------
------*/
/*------------------------------------------格式化
----------------------------------------------*/
void format()
{
	int DISKSIZE = 1024; /*磁盘块的大小 1K*/
	int DIR_LENGTH = 255; /*路径最长可达255字节*/
	int DISK_NUM = MEM_D_SIZE / DISKSIZE; /*磁盘块数目 102400=100M/1K*/
	int EXT2SIZE = 8 * 1024; /*EXT2表大小 8K=8192B*/
	int ROOT_DISK_NO = EXT2SIZE / DISKSIZE + 1; /*根目录起始盘快号 9*/
	int ROOT_DISK_SIZE = 1024;/*根目录大小1k*/
	int i;
	FILE *fp;
	boot_block = (struct ext2_boot_block *)(fdisk);/*前面的1k空间分配给引导块*/
												   /*--------------------下面的属于一
												   个组块---------------------------------*/
	super_block = (struct ext2_super_block*)(fdisk + DISKSIZE);/*分配1k空间给超级块*/
	group_desc = (struct ext2_group_desc*)(fdisk + 2 * DISKSIZE);/*分配1k给组块描述符*/
	data_block_bitmap = (struct ext2_data_block_bitmap*)(fdisk + 3 * DISKSIZE);/*分配1k的空
																			   间给数据盘块位图*/
	inode_bitmap = (struct ext2_inode_bitmap*)(fdisk + 4 * DISKSIZE);//分配1k的空间给索引节点盘块位图
		inode = (struct ext2_inode *)(fdisk + 5 * DISKSIZE); /*计算EXE2索引表的起始地址,占8k*/
	root = (struct ext2_dir_entry *)(fdisk + 5 * DISKSIZE + EXT2SIZE); /*根目录的地址*/
																	   /*------引导
																	   块初始化--------*/
	boot_block->address_ext2_super_block = DISKSIZE;
	boot_block->address_ext2_group_desc = 2 * DISKSIZE;
	boot_block->address_ext2_data_block_bitmap = 3 * DISKSIZE;
	boot_block->address_ext2_inode_bitmap = 4 * DISKSIZE;
	boot_block->address_ext2_inode_table = 5 * DISKSIZE;
	boot_block->address_ext_root = 5 * DISKSIZE + EXT2SIZE;
	/*-----超级块的初始化-------*/
	super_block->s_blocks_count = DISK_NUM;
	super_block->s_first_data_block = 0;
	super_block->s_free_blocks_count = DISK_NUM - 14;//前面的13个盘块属于元数据盘块，第14个是存储根目录的盘块，后面的所有盘块用于做数据盘块
		super_block->s_free_inodes_count = DISK_NUM - ROOT_DISK_NO;
	super_block->s_log_block_size = DISKSIZE;
	super_block->s_dir_length = DIR_LENGTH;
	super_block->s_max_size = MAX_WRITE;
	super_block->s_mofn = MOFN;
	super_block->s_msd = MSD;
	super_block->s_root_disk_no = ROOT_DISK_NO;
	super_block->s_root_disk_size = ROOT_DISK_SIZE;
	/*--------组块描述符初始化-------*/
	group_desc->bg_block_bitmap = 4;
	group_desc->bg_inode_bitmap = 5;
	group_desc->bg_inode_table = 6;
	group_desc->bg_inode_table_size = EXT2SIZE;
	/*-----------数据盘块位图初始化---------------*/
	data_block_bitmap->first_block = 14;
	data_block_bitmap->free_blocks = DISK_NUM - 14;
	data_block_bitmap->use_blocks = 1;
	/*-----------索引节点盘块位图初始化---------------*/
	inode_bitmap->first_inode = 0;
	inode_bitmap->free_inodes = DISK_NUM - ROOT_DISK_NO;
	inode_bitmap->use_inodes = ROOT_DISK_NO;
	/*---------索引表的初始化----------*/
	for (i = 1; i<ROOT_DISK_NO - 1; i++) 
	{
		inode[i].i_block = i + 1;
		inode[i].em_disk = '1';
	}
	inode[ROOT_DISK_NO - 1].i_block = -1;
	inode[ROOT_DISK_NO - 1].em_disk = '1';
	inode[ROOT_DISK_NO].i_block = -1; /*存放根目录的磁盘块号*/
	inode[ROOT_DISK_NO].em_disk = '1';
	for (i = ROOT_DISK_NO + 1; i<EXT2SIZE / sizeof(inode[0]) - ROOT_DISK_NO; i++)
	{
		inode[i].i_block = -1;
		inode[i].em_disk = '0';
	}
	/*初始化目录*/
	/*---------指向当前目录的目录项---------*/
	root->directitem[0].sign = 1;
	root->directitem[0].firstdisk = ROOT_DISK_NO;
	strcpy(root->directitem[0].name, ".");
	root->directitem[0].next = root->directitem[0].firstdisk;
	root->directitem[0].property = '1';
	root->directitem[0].size = ROOT_DISK_SIZE;
	/*-------指向上一级目录的目录项---------*/
	root->directitem[1].sign = 1;
	root->directitem[1].firstdisk = ROOT_DISK_NO;
	strcpy(root->directitem[1].name, "..");
	root->directitem[1].next = root->directitem[0].firstdisk;
	root->directitem[1].property = '1';
	root->directitem[1].size = ROOT_DISK_SIZE;
	for (i = 2; i<MSD + 2; i++) /*-子目录初始化为空-*/
	{
		root->directitem[i].sign = 0;
		root->directitem[i].firstdisk = -1;
		strcpy(root->directitem[i].name, "");
		root->directitem[i].next = -1;
		root->directitem[i].property = '0';
		root->directitem[i].size = 0;
	}
	if ((fp = fopen("disk.dat", "wb")) == NULL)
	{
		printf("错误，无法打开文件!\n");
		return;
	}
	if (fwrite(fdisk, MEM_D_SIZE, 1, fp) != 1) /*把虚拟磁盘空间保存到磁盘文件中*/
	{
		printf("错误，文件写入错误! \n");
	}
	fclose(fp);
}
/*--------------------------------进入文件系统
--------------------------------------------------*/
void enter()
{
	FILE *fp;
	int i;
	fdisk = (char *)malloc(MEM_D_SIZE*sizeof(char)); /*申请 100M空间*/
	if ((fp = fopen("disk.dat", "rb")) == NULL)
	{
		printf("错误，文件打开失败！\n");
		return;
	}
	if (!fread(fdisk, MEM_D_SIZE, 1, fp)) /*把磁盘文件disk.dat 读入虚拟磁盘空间(内存)*/
	{
		printf("错误，文件读取失败！\n");
		exit(0);
	}
	boot_block = (struct ext2_boot_block *)(fdisk);/*前面的1k空间分配给引导块*/
												   /*---------下面所有盘块的相对偏移
												   位置是由引导块保存---------*/
	super_block = (struct ext2_super_block*)(fdisk + boot_block->address_ext2_super_block);/*
																						   分配1k空间给超级块*/
	group_desc = (struct ext2_group_desc*)(fdisk + boot_block->address_ext2_group_desc);/*
																						分配1k给组块描述符*/
	data_block_bitmap = (struct ext2_data_block_bitmap*)(fdisk +
		boot_block->address_ext2_data_block_bitmap);/*分配1k的空间给数据盘块位图*/
	inode_bitmap = (struct ext2_inode_bitmap*)(fdisk +
		boot_block->address_ext2_inode_bitmap);//分配1k的空间给索引节点盘块位图
	inode = (struct ext2_inode *)(fdisk + boot_block->address_ext2_inode_table); /*计算EXE2
																				 索引表的起始地址,占用8k空间*/
	root = (struct ext2_dir_entry *)(fdisk + boot_block->address_ext_root); /*根目录的地址
																			*/
	fclose(fp);
	/*--------------初始化用户打开表------------------*/
	for (i = 0; i<super_block->s_mofn; i++)
	{
		strcpy(u_opentable.openitem[i].name, "");
		u_opentable.openitem[i].firstdisk = -1;
		u_opentable.openitem[i].size = 0;
	}
	u_opentable.cur_size = 0;
	cur_dir = root; /*当前目录为根目录*/
	bufferdir = (char *)malloc(super_block->s_dir_length*sizeof(char));
	strcpy(bufferdir, "root"); /*显示根目录为root*/
}
/*----------------------------------------------------------------------------------------
------*/
/*------------------------------------退出文件系统
----------------------------------------------*/
void halt()
{
	FILE *fp;
	int i;
	if ((fp = fopen("disk.dat", "wb")) == NULL)
	{
		printf("E错误，文件打开失败！\n");
		return;
	}
	if (!fwrite(fdisk, MEM_D_SIZE, 1, fp)) /*把虚拟磁盘空间(内存)内容读入磁盘文件disk.dat */
	{
		printf("错误，文件写入失败!\n");
	}
	fclose(fp);
	free(fdisk);
	free(bufferdir);
	return;
}
/*----------------------------------------------------------------------------------------
------*/
/*----------------------------------------创建文件
----------------------------------------------*/
int create(char *name)
{
	int i, j;
	super_block->s_free_blocks_count--;
	super_block->s_free_inodes_count--;
	super_block->s_free_blocks_count--;
	super_block->s_free_inodes_count--;
	data_block_bitmap->use_blocks++;
	data_block_bitmap->free_blocks--;
	inode_bitmap->free_inodes--;
	inode_bitmap->use_inodes++;
	if (strlen(name)>8) /*文件名大于 8位*/
		return(-1);
	for (i = 2; i<super_block->s_msd + 2; i++) /*找到第一个空闲子目录*/
	{
		if (cur_dir->directitem[i].firstdisk == -1)
			break;
	}
	for (j = 2; j<super_block->s_msd + 2; j++) /*检查创建文件是否与已存在的文件重名*/
	{
		if (!strcmp(cur_dir->directitem[j].name, name))
			break;
	}
	if (i >= super_block->s_msd + 2) /*无空目录项*/
		return(-2);
	if (u_opentable.cur_size >= super_block->s_mofn) /*打开文件太多*/
		return(-3);
	if (j<super_block->s_msd + 2) /*文件已经存在*/
		return(-4);
	for (j = super_block->s_root_disk_no + 1; j<super_block->s_blocks_count; j++) /*找到空
																				  闲盘块 j 后退出*/
	{
		if (inode[j].em_disk == '0')
			break;
	}
	if (j >= super_block->s_blocks_count)
		return(-5);
	inode[j].em_disk = '1'; /*将索引节点置为非空闲*/
							/*-----------填写目录项-----------------*/
	strcpy(cur_dir->directitem[i].name, name);
	cur_dir->directitem[i].firstdisk = j;
	cur_dir->directitem[i].size = 0;
	cur_dir->directitem[i].next = j;
	cur_dir->directitem[i].property = '0';
	/*cur_dir->directitem[i].sign 丢失*/
	/*---------------------------------*/
	fd = open(name); /*打开所创建的文件*/
	return 0;
}
/*----------------------------------------打开文件
----------------------------------------------*/
int open(char *name)
{
	int i, j;
	for (i = 2; i<super_block->s_msd + 2; i++) /*文件是否存在*/
	{
		if (!strcmp(cur_dir->directitem[i].name, name))
			break;
	}
	if (i >= super_block->s_msd + 2) /*文件不存在*/
		return(-1);
	/*--------是文件还是目录-----------------------*/
	if (cur_dir->directitem[i].property == '1')/*是目录，不可打开读写*/
		return(-4);
	/*--------文件是否打开-----------------------*/
	for (j = 0; j<super_block->s_mofn; j++)
	{
		if (!strcmp(u_opentable.openitem[j].name, name))
			return(-2);
	}
	if (u_opentable.cur_size >= super_block->s_mofn) /*文件打开太多*/
		return(-3);
	/*--------查找一个空闲用户打开表项-----------------------*/
	for (j = 0; j<super_block->s_mofn; j++)
	{
		if (u_opentable.openitem[j].firstdisk == -1)
			break;
	}
	/*--------------填写表项的相关信息------------------------*/
	u_opentable.openitem[j].firstdisk = cur_dir->directitem[i].firstdisk;
	strcpy(u_opentable.openitem[j].name, name);
	u_opentable.openitem[j].size = cur_dir->directitem[i].size;
	u_opentable.cur_size++;
	/*----------返回用户打开表表项的序号--------------------------*/
	return(j);
}
/*----------------------------------------关闭文件
----------------------------------------------*/
int close(char *name)
{
	int i;
	for (i = 0; i<super_block->s_mofn; i++)
	{
		if (!strcmp(u_opentable.openitem[i].name, name))
			break;
	}
	if (i >= super_block->s_mofn) /*--文件没有打开-*/
		return(-1);
	/*-----------清空该文件的用户打开表项的内容---------------------*/
	strcpy(u_opentable.openitem[i].name, "");
	u_opentable.openitem[i].firstdisk = -1;
	u_opentable.openitem[i].size = 0;
	u_opentable.cur_size--;
	fd = -1; /*文件打开表的序号为 -1 */
	return 0;
}
/*----------------------------------------------------------------------------------------
------*/
/*----------------------------------------删除文件
----------------------------------------------*/
int del(char *name)
{
	int i, cur_item, item, temp;
	super_block->s_free_blocks_count++;
	super_block->s_free_inodes_count++;
	super_block->s_free_blocks_count++;
	super_block->s_free_inodes_count++;
	data_block_bitmap->use_blocks--;
	data_block_bitmap->free_blocks++;
	inode_bitmap->free_inodes++;
	inode_bitmap->use_inodes--;
	for (i = 2; i<super_block->s_mofn + 2; i++) /*--查找要删除文件是否在当前目录中-*/
	{
		if (!strcmp(cur_dir->directitem[i].name, name))
			break;
	}
	cur_item = i; /*--用来保存目录项的序号,供释放目录中-*/
	if (i >= super_block->s_mofn + 2) /*--如果不在当前目录中-*/
		return(-1);
	if (cur_dir->directitem[cur_item].property != '0') /*--如果删除的是目录-*/
		return(-3);
	for (i = 0; i<super_block->s_mofn; i++) /*--如果文件打开,则不能删除,退出-*/
	{
		if (!strcmp(u_opentable.openitem[i].name, name))
			return(-2);
	}
	item = cur_dir->directitem[cur_item].firstdisk;/*--该文件的起始盘块号-*/
	while (item != -1) /*--释放空间,将EXT2表对应项进行修改-*/
	{
		temp = inode[item].i_block;
		inode[item].i_block = -1;
		inode[item].em_disk = '0';
		item = temp;
	}
	/*-----------------释放目录项-----------------------*/
	cur_dir->directitem[cur_item].sign = 0;
	cur_dir->directitem[cur_item].firstdisk = -1;
	strcpy(u_opentable.openitem[cur_item].name, "");
	cur_dir->directitem[cur_item].next = -1;
	cur_dir->directitem[cur_item].property = '0';
	cur_dir->directitem[cur_item].size = 0;
	return 0;
}
/*-------------------------------显示当前目录的子目录
-------------------------------------------*/
void dir()
{
	int i;
	for (i = 2; i<super_block->s_msd + 2; i++)
	{
		if (cur_dir->directitem[i].firstdisk != -1) /*-如果存在子目录-*/
			printf("%s ", cur_dir->directitem[i].name);
	}
	printf("\n");
}
/*----------------------------------------------------------------------------------------
------*/
/*---------------------------------------显示当前路径
-------------------------------------------*/
void show()
{
	printf("%s>", bufferdir);
}
/*----------------------------------------------------------------------------------------
------*/
/*--------------------------------------输出提示信息
--------------------------------------------*/
void help()
{
	printf("********************************************************************************\n");
printf("文件创建成功，文件大小为100M \n");		
printf("Welcome to DOS File system!\n");
	printf("--------------------------------------------------------------------------------\n");
		printf("[退出系统 halt]\n");
	printf("[创建文件>> create+文件名]\n");
	printf("[删除文件>> del+文件名]\n");
	printf("[打开文件>> open+文件名]\n");
	printf("[关闭文件>> close+文件名]\n");
	printf("[写文件>> write]\n");
	printf("[读文件>> read]\n");
	printf("[创建子目录>> mkdir+目录名]\n");
	printf("[删除子目录>> rmdir+目录名 ]\n");
	printf("[显示当前目录的子目录 dir]\n");
	printf("[更改当前目录 cd+目录名]\n");
	printf("--------------------------------------------------------------------------------\n");
}
/*----------------------------------------写文件
------------------------------------------------*/
int write(int fd, char *buf, int len)
{
	char *first;
	int item, i, j, k;
	int temp;
	/*----------用 $ 字符作为空格 # 字符作为换行符-----------------------*/
	char Space = 32; /*SPACE的ASCII码值*/
	char Endter = '\n';
	for (i = 0; i<len; i++)
	{
		if (buf[i] == '$') /*用 $ 字符作为空格*/
			buf[i] = Space;
		else if (buf[i] == '#')
			buf[i] = Endter;
	}
	/*----------读取用户打开表对应表项第一个盘块号-----------------------*/
	item = u_opentable.openitem[fd].firstdisk;
	/*-------------找到当前目录所对应表项的序号-------------------------*/
	for (i = 2; i<super_block->s_msd + 2; i++)
	{
		if (cur_dir->directitem[i].firstdisk == item)
			break;
	}
	temp = i;
	/*-----计算写入该文件的数据盘块的首地址-------*/
	first = fdisk + (5 + item)*super_block->s_log_block_size +
		u_opentable.openitem[fd].size%super_block->s_log_block_size;
	/*-----如果最后磁盘块剩余的大小大于要写入的文件的大小-------*/
	if (super_block->s_log_block_size -
		u_opentable.openitem[fd].size%super_block->s_log_block_size>len)
	{
		strcpy(first, buf);
		u_opentable.openitem[fd].size = u_opentable.openitem[fd].size + len;
		cur_dir->directitem[temp].size = cur_dir->directitem[temp].size + len;
	}
	return 0;
}
/*----------------------------------------读文件
------------------------------------------------*/
int read(int fd, char *buf)
{
	int len = u_opentable.openitem[fd].size;/*获取文件大小*/
	char *first;
	int i, j, item;
	int ilen1, modlen;
	item = u_opentable.openitem[fd].firstdisk;/*获取文件指向的第一个盘块号*/
	if (len>u_opentable.openitem[fd].size) /*--欲读出的文件长度比实际文件长度长-*/
		return(-1);
	first = fdisk + (5 + item)*super_block->s_log_block_size; /*--计算文件的起始位置-*/
			for (j = 0; j<len - i*super_block->s_log_block_size; j++)
				buf[i*super_block->s_log_block_size + j] = first[j];
	return 0;
}
/*---------------------------------------创建子目录
---------------------------------------------*/
int mkdir(char *name)
{
	int i, j;
	struct ext2_dir_entry *cur_mkdir;
	if (strchr(name, '/'))/*如果目录名中有 '/'字符*/
		return(-4);
	if (!strcmp(name, "."))
		return(-6);
	if (!strcmp(name, ".."))
		return(-6);
	if (strlen(name)>8) /*-如果目录名长度大于 8位-*/
		return(-1);
	for (i = 2; i<super_block->s_msd + 2; i++) /*-如果有空闲目录项退出-*/
	{
		if (cur_dir->directitem[i].firstdisk == -1)
			break;
	}
	if (i >= super_block->s_msd + 2) /*-目录/文件 已满-*/
		return(-2);
	for (j = 2; j<super_block->s_msd + 2; j++) /*-判断是否有重名-*/
	{
		if (!strcmp(cur_dir->directitem[j].name, name))
			break;
	}
	if (j<super_block->s_msd + 2) /*-如果有重名-*/
		return(-3);
	for (j = super_block->s_root_disk_no + 1; j<super_block->s_blocks_count; j++) /*-找到空
																				  闲磁盘块 j 后退出-*/
	{
		if (inode[j].em_disk == '0')
			break;
	}
	if (j >= super_block->s_blocks_count)
		return(-5);
	inode[j].em_disk = '1'; /*-将该空闲块设置为已分配-*/
							/*-------------填写目录项----------*/
	strcpy(cur_dir->directitem[i].name, name);
	cur_dir->directitem[i].firstdisk = j;
	cur_dir->directitem[i].size = super_block->s_root_disk_size;
	cur_dir->directitem[i].next = j; /*-指向子目录(其实就是其本身)的起始盘块号-*/
	cur_dir->directitem[i].property = '1';
	/*-sign=1为根标志,这里可以省略-*/
	/*-所创目录在虚拟磁盘上的地址(内存物理地址)-*/
	cur_mkdir = (struct ext2_dir_entry *)(fdisk + (5 +
		cur_dir->directitem[i].firstdisk)*super_block->s_log_block_size);
	/*-初始化目录-*/
	/*-指向当前目录的目录项-*/
	cur_mkdir->directitem[0].sign = 0;
	cur_mkdir->directitem[0].firstdisk = cur_dir->directitem[i].firstdisk;
	strcpy(cur_mkdir->directitem[0].name, ".");
	cur_mkdir->directitem[0].next = cur_mkdir->directitem[0].firstdisk;
	cur_mkdir->directitem[0].property = '1';
	cur_mkdir->directitem[0].size = super_block->s_root_disk_size;
	/*-指向上一级目录的目录项-*/
	cur_mkdir->directitem[1].sign = cur_dir->directitem[0].sign;/*-指向上一级目录的目录项
																-*/
	cur_mkdir->directitem[1].firstdisk = cur_dir->directitem[0].firstdisk;
	strcpy(cur_mkdir->directitem[1].name, "..");
	cur_mkdir->directitem[1].next = cur_mkdir->directitem[1].firstdisk;
	cur_mkdir->directitem[1].property = '1';
	cur_mkdir->directitem[1].size = super_block->s_root_disk_size;
	for (i = 2; i<super_block->s_msd + 2; i++) /*-子目录都初始化为空-*/
	{
		cur_mkdir->directitem[i].sign = 0;
		cur_mkdir->directitem[i].firstdisk = -1;
		strcpy(cur_mkdir->directitem[i].name, "");
		cur_mkdir->directitem[i].next = -1;
		cur_mkdir->directitem[i].property = '0';
		cur_mkdir->directitem[i].size = 0;
	}
	return 0;
}
/*----------------------------------------------------------------------------------------
------*/
/*---------------------------------------删除子目录
---------------------------------------------*/
int rmdir(char *name)
{
	int i, j, item;
	struct ext2_dir_entry *temp_dir;
	/*-检查当前目录项中有无该目录-*/
	for (i = 2; i<super_block->s_msd + 2; i++)
	{
		if (!strcmp(cur_dir->directitem[i].name, name))
			break;
	}
	if (cur_dir->directitem[i].property != '1')/*-删除的不是目录-*/
		return(-3);
	if (i >= super_block->s_msd + 2) /*-没有这个文件或目录-*/
		return(-1);
	/*-判断要删除的目录有无子目录-*/
	/*-要删除的目录起始地址-*/
	temp_dir = (struct ext2_dir_entry *)(fdisk + (5 +
		cur_dir->directitem[i].next)*super_block->s_log_block_size);
	for (j = 2; j<super_block->s_msd + 2; j++)
	{
		if (temp_dir->directitem[j].next != -1)
			break;
	}
	if (j<super_block->s_msd + 2) /*-有子目录或文件-*/
		return(-2);
	/*------------找到起始盘块号,并将其释放----------------*/
	item = cur_dir->directitem[i].firstdisk;
	inode[item].em_disk = '0';
	/*-修改目录项-*/
	cur_dir->directitem[i].sign = 0;
	cur_dir->directitem[i].firstdisk = -1;
	strcpy(cur_dir->directitem[i].name, "");
	cur_dir->directitem[i].next = -1;
	cur_dir->directitem[i].property = '0';
	cur_dir->directitem[i].size = 0;
	return 0;
}
/*---------------------------------------更改当前目录
-------------------------------------------*/
int cd(char *name)
{
	int i, j, item;
	char *str, *str1;
	char *temp, *point, *point1;
	struct ext2_dir_entry *temp_dir;
	temp_dir = cur_dir; /*-先用临时目录代替当前目录-*/
	str = name; /*-str用来记录下次查找的起始地址-*/
	if (!strcmp("/", name)) /*如果输入"/" ,回根目录*/
	{
		cur_dir = root;
		strcpy(bufferdir, "root");
		return 0;
	}
	j = 0;
	for (i = 0; i<(int)strlen(str); i++)/*查找有两个连续是"/",即"//",退出 */
	{
		if (name[i] == '/')
		{
			j++;
			if (j >= 2)
			{
				return -3;
			}
		}
		else
			j = 0;
	}
	if (name[0] == '/') /*第一个是/的回到根目录*/
	{
		temp_dir = root;
		strcpy(bufferdir, "root");
		str++;
	}
	if (str[strlen(str) - 1] == '/')/*如果最后一个是"/" ,去掉这个"/"*/
	{
		str[strlen(str) - 1] = '\0';
	}
	str1 = strchr(str, '/'); /*-找到'/'字符的位置-*/
	temp = (char *)malloc(super_block->s_dir_length *sizeof(char));/*-为子目录的名字分配空
																   间-*/
	while (str1 != NULL) /*-找到-*/
	{
		for (i = 0; i<str1 - str; i++)
		{
			temp[i] = str[i];
		}
		temp[i] = '\0';
		for (j = 2; j<super_block->s_msd + 2; j++) /*-查找该子目录是否在当前目录中-*/
		{
			if (!strcmp(temp_dir->directitem[j].name, temp))
				break;
		}
		if (j >= super_block->s_msd + 2) /*-不在当前目录-*/
			return(-1);
		item = temp_dir->directitem[j].firstdisk;
		temp_dir = (struct ext2_dir_entry *)(fdisk + item*super_block->s_log_block_size); /*-
																						  计算当前目录物理位置-*/
		str = str1 + 1;
		str1 = strchr(str, '/');
		//free(temp);
	}
	str1 = str1 + strlen(str);
	for (i = 0; i<(int)strlen(str); i++)
		temp[i] = str[i];
	temp[i] = '\0';
	for (j = 0; j<super_block->s_msd + 2; j++) /*-查找该子目录是否在当前目录中-*/
	{
		if (!strcmp(temp_dir->directitem[j].name, temp))
			break;
	}
	free(temp);/*释放申请的临时空间*/
	if (temp_dir->directitem[j].property != '1') /*-打开的不是目录-*/
		return(-2);
	if (j >= MSD + 2) /*-不在当前目录-*/
		return(-1);
	item = temp_dir->directitem[j].firstdisk;
	/*-当前目录在磁盘中位置-*/
	temp_dir = (struct ext2_dir_entry *)(fdisk + (5 + item)*super_block->s_log_block_size);
	if (!strcmp("..", name))
	{
		if (cur_dir->directitem[j - 1].sign != 1) /*-如果子目录不是根目录-*/
		{
			point = strchr(bufferdir, '/');
			while (point != NULL)
			{
				point1 = point + 1; /*-减去'/'所占的空间,记录下次查找的起始地址-*/
				point = strchr(point1, '/');
			}
			*(point1 - 1) = '\0'; /*-将上一级目录删除-*/
		}
	}
	else if (!strcmp(".", name))
	{
		bufferdir = bufferdir; /*-如果是当前目录则不变-*/
	}
	else
	{
		if (name[0] != '/')
			bufferdir = strcat(bufferdir, "/"); /*-修改当前目录-*/
		bufferdir = strcat(bufferdir, name);
	}
	cur_dir = temp_dir; /*-将当前目录确定下来-*/
	return 0;
}
int main() {
	FILE *fp;
	char ch;
	char a[100];
	char code[11][10];
	char name[10];
	int i, flag, r_size;
	char *content;
	content = (char *)malloc(MAX_WRITE*sizeof(char));
	if ((fp = fopen("disk.dat", "rb")) == NULL)/*如果还没有进行格式化，则要格式化*/
	{
		printf("Y你还没有格式化，需要格式化吗(y/n)");
		scanf("%c", &ch);
		if (ch == 'y')
		{
			initfile();
			printf("格式化成功! \n");
		}
		else
		{
			return 0;
		}
	}
	enter();
	help();
	show();
	/*将命令全部保存在CODE数组中*/
	strcpy(code[0], "halt");
	strcpy(code[1], "create");
	strcpy(code[2], "open");
	strcpy(code[3], "close");
	strcpy(code[4], "write");
	strcpy(code[5], "read");
	strcpy(code[6], "del");
	strcpy(code[7], "mkdir");
	strcpy(code[8], "rmdir");
	strcpy(code[9], "dir");
	strcpy(code[10], "cd");
	while (1)
	{
		scanf("%s", a);
		for (i = 0; i<11; i++)
		{
			if (!strcmp(code[i], a))
				break;
		}
		switch (i)
		{
		case 0: //*--退出文件系统--//
			free(content);
			halt();
			return 0;
		case 1: //*--创建文件--//
			scanf("%s", name);
			flag = create(name);
			if (flag == -1)
			{
				printf("错误，长度过长!\n");
			}
			else if (flag == -2)
			{
				printf("错误，目标文件已满 !\n");
			}
			else if (flag == -3)
			{
				printf("错误，打开文件数量超载!\n");
			}
			else if (flag == -4)
			{
				printf("错误，该文件名已存在 !\n");
			}
			else if (flag == -5)
			{
				printf("错误，磁盘已满!\n");
			}
			else
			{
				printf("文件创建成功! \n");
			}
			show();
			break;
		case 2://--打开文件--//
			scanf("%s", name);
			fd = open(name);
			if (fd == -1)
			{
				printf("错误，文件打开并没退出! \n");
			}
			else if (fd == -2)
			{
				printf("Error:该文件已被访问！ \n");
			}
			else if (fd == -3)
			{
				printf("错误，打开文件数量超载! \n");
			}
			else if (fd == -4)
			{
				printf("错误: \n 这是目标，无法读取或写入! \n");
			}
			else
			{
				printf("打开文件成功! \n");
			}
			show();
			break;
		case 3://--关闭文件--//
			scanf("%s", name);
			flag = close(name);
			if (flag == -1)
			{
				printf("错误，文件没有被访问! \n");
			}
			else
			{
				printf("退出完毕! \n");
			}
			show();
			break;
		case 4:/*--写文件--*/
			if (fd == -1)
			{
				printf("错误，文件没被访问! \n");
			}
			else
			{
				printf("请输入文件内容:");
				scanf("%s", content);
				flag = write(fd, content, strlen(content));
				if (flag == 0)
				{
					printf("输入成功! \n");
				}
				else
				{
					printf("错误，磁盘内存不够！ \n");
				}
			}
			show();
			break;
		case 5:/*--读文件--*/
			if (fd == -1)
			{
				printf("错误，文件没被访问! \n");
			}
			else
			{
				flag = read(fd, content);
				if (flag == -1)
				{
					printf("错误，文件大小超过了内存大小! \n");
				}
				else
				{
					//printf("读取成功! /n 内容如下 :");
					for (i = 0; i<u_opentable.openitem[fd].size; i++)
					{
						printf("%c", content[i]);
					}
					printf("\n");
				}
			}
			show();
			break;
			break;
		case 6://*--删除文件--
			scanf("%s", name);
			flag = del(name);
			if (flag == -1)
			{
				printf("错误，文件没被退出! \n");
			}
			else if (flag == -2)
			{
				printf("错误，文件正在使用，请关闭 ! \n");
			}
			else if (flag == -3)
			{
				printf("错误，无法删除 ! \n");
			}
			else
			{
				printf("删除成功! \n");
			}
			show();
			break;
		case 7://*--创建子目录--/
			scanf("%s", name);
			flag = mkdir(name);
			if (flag == -1)
			{
				printf("错误，目录名过长! \n");
			}
			else if (flag == -2)
			{
				printf("错误，目标文件已满! \n");
			}
			else if (flag == -3)
			{
				printf("错误，改名已被使用! \n");
			}
			else if (flag == -4)
			{
				printf("错误，无法命名! \n");
			}
			else if (flag == -5)
			{
				printf("错误，磁盘已满!\n");
			}
			else if (flag == -6)
			{
				printf("错误: '..' or '.' 在同一个文件!\n");
			}
			else if (flag == 0)
			{
				printf("定向成功! \n");
			}
			show();
			break;
		case 8://*--删除子目录--/
			scanf("%s", name);
			flag = rmdir(name);
			if (flag == -1)
			{
				printf("错误，文件没被退出! \n");
			}
			else if (flag == -2)
			{
				printf("错误，该目录已有子目录，请先删除子目录!\n");
			}
			else if (flag == -3)
			{
				printf("错误，无法删除子目录! \n");
			}
			else if (flag == 0)
			{
				printf("目录删除成功! \n");
			}
			show();
			break;
		case 9://*--显示当前子目录--/
			dir();
			show();
			break;
		case 10:/*--更改当前目录--*/
			scanf("%s", name);
			flag = cd(name);
			if (flag == -1)
			{
				printf("错误，路径不正确!\n");
			}
			else if (flag == -2)
			{
				printf("错误，目录打开失败!\n");
			}
			else if (flag == -3)
			{
				printf("Error:The '//' is too much !\n");
			}
			show();
			break;
		default:
			printf("命令错误！! \n");
			show();
		}
  	} 
}
