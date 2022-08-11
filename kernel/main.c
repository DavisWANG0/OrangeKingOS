#include "type.h"
#include "stdio.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "proto.h"

#include "time.h"
#include "termio.h"
#include "signal.h"
#include "sys/time.h"

#define DELAY_TIME 5000
#define NULL ((void*)0)

/*用户级应用：游戏2048模块 */

#define GO_UP    0x41
#define GO_DOWN  0x42
#define GO_LEFT  0x44
#define GO_RIGHT 0x43
#define GO_QUIT  0x71

static void init_game();//初始化
static void loop_game(int fd_stdin);//循环
static void reset_game();//重置
static void release_game(int signal);//释放资源 
static char* read_keyboard(int fd_stdin);//读取键盘控制
static void move_left();
static void move_right();
static void move_up();
static void move_down();

static void add_rand_num();//生成随机数，本程序中仅生成2或4，概率之比设为4:1
static void check_game_over();//检测是否输掉游戏，设定游戏结束标志
static int get_null_count();//获取游戏面板上空位置数量
static void clear_screen();//清屏
static void refresh_screen();//刷新界面

static int board[4][4];//游戏数字面板
static int score;//游戏得分
static int best;//游戏最高分
static int if_need_add_num;//是否需要生成随机数标志，1表示需要，0表示不需要
static int if_game_over;//是否游戏结束标志，1表示游戏结束，0表示正常
static int if_prepare_exit;//是否准备退出游戏，1表示是，0表示否

struct termios old_config;//linux下终端属性配置备份
static char config_path[4096] = { 0 };//配置文件路径

/*游戏2048主函数 */
void GAME2048(fd_stdin, fd_stdout)
{
	clear();
	init_game();
	loop_game(fd_stdin);
	release_game(0);
}

/*循环游戏 */
void loop_game(int fd_stdin) {
	while (1) {
		char rdbuf[128];
		int r = 0;
		r = read(fd_stdin, rdbuf, 70);
		if (r > 1)
		{
			refresh_screen();
			continue;
		}
		rdbuf[r] = 0;
		char cmd = rdbuf[0];
		//判断是否退出游戏
		if (if_prepare_exit) {
			if (cmd == 'y' || cmd == 'Y') {
				clear_screen();
				//清屏后退出游戏
				return;
			}
			else if (cmd == 'n' || cmd == 'N') {
				if_prepare_exit = 0;
				refresh_screen();//取消退出
				continue;
			}
			else {
				continue;
			}
		}
		//判断是否已经输掉游戏
		if (if_game_over) {
			if (cmd == 'y' || cmd == 'Y') {
				reset_game();
				continue;
			}
			else if (cmd == 'n' || cmd == 'N') {
				clear();//取消 
				return;
			}
			else continue;
		}
		if_need_add_num = 0;//先设定不默认需要生成随机数，需要时再设定为1
		//指令判断，wsad代表上下左右命令，q代表退出
		switch (cmd) {
		case 'a':
			move_left();
			break;
		case 's':
			move_down();
			break;
		case 'w':
			move_up();
			break;
		case 'd':
			move_right();
			break;
		case 'q':
			if_prepare_exit = 1;
			break;
		default:
			refresh_screen();
			continue;
		}

		//生成随机数同时刷新显示，
		if (if_need_add_num) {
			add_rand_num();
			refresh_screen();
		}
		else if (if_prepare_exit) {
			refresh_screen();
		}
	}
}

/*重置游戏 */
void reset_game() {
	//初始化 
	score = 0;if_need_add_num = 1;if_game_over = 0;if_prepare_exit = 0;
	//先随机生成一个2，其他均为0
	int n = get_ticks() % 16;
	int i;
	for (i = 0; i < 4; ++i) {
		int j;
		for (j = 0; j < 4; ++j) {
			board[i][j] = (n-- == 0 ? 2 : 0);
		}
	}
	//再生成一个随机的2或者4
	add_rand_num();
	//在这里刷新界面并显示的时候，界面上已经默认出现了两个数字，其他的都为空（值为0）
	refresh_screen();
}

/*生成随机数 */
void add_rand_num() {
	int n = get_ticks() % get_null_count();//确定在何处空位置生成随机数
	int i;
	for (i = 0; i < 4; ++i) {
		int j;
		for (j = 0; j < 4; ++j) {
			//定位生成位置 
			if (board[i][j] == 0 && n-- == 0) {
				board[i][j] = (get_ticks() % 5 ? 2 : 4);
				//通过除以五是否为零来生成数字2或4，生成概率为4:1
				return;
			}
		}
	}
}
/*获取空位置数量 */
int get_null_count() {
	int n = 0;
	int i;
	for (i = 0; i < 4; ++i) {
		int j;
		for (j = 0; j < 4; ++j) {
			board[i][j] == 0 ? ++n : 1;
		}
	}
	return n;
}
/*检查是否结束 */
void check_game_over() {
	int i;
	for (i = 0; i < 4; ++i) {
		int j;
		for (j = 0; j < 3; ++j) {
			//横向和纵向比较相邻的两个元素是否相等，若有相等则游戏不结束
			if (board[i][j] == board[i][j + 1] || board[j][i] == board[j + 1][i]) {
				if_game_over = 0;
				return;
			}
		}
	}
	if_game_over = 1;
}
/*左移 */
void move_left() {
	//变量i用来遍历行项的下标，并且在移动时所有行相互独立，互不影响
	int i;
	for (i = 0; i < 4; ++i) {
		//变量j为列下标，变量k为待比较（合并）项的下标，循环进入时k<j
		int j, k;
		for (j = 1, k = 0; j < 4; ++j) {
			if (board[i][j] > 0)
			{
				if (board[i][k] == board[i][j]) {
					//情况1：k项和j项相等，此时合并方块并计分
					score += board[i][k++] *= 2;
					board[i][j] = 0;
					if_need_add_num = 1;//需要生成随机数和刷新界面
				}
				else if (board[i][k] == 0) {
					//情况2：k项为空，j方块移动到k方块
					board[i][k] = board[i][j];
					board[i][j] = 0;
					if_need_add_num = 1;
				}
				else {
					//情况3：k项不为空，且和j项不相等，相当于移动到k+1的位置
					board[i][++k] = board[i][j];
					if (j != k) {
						//判断j项和k项是否原先就挨在一起，若不是则把j项赋值为0
						board[i][j] = 0;
						if_need_add_num = 1;
					}
				}
			}
		}
	}
}

/*右移  */
void move_right() {
	//仿照左移操作，区别仅仅是j和k都反向遍历
	int i;
	for (i = 0; i < 4; ++i) {
		int j, k;
		for (j = 2, k = 3; j >= 0; --j) {
			if (board[i][j] > 0) {
				if (board[i][k] == board[i][j]) {
					score += board[i][k--] *= 2;
					board[i][j] = 0;
					if_need_add_num = 1;
				}
				else if (board[i][k] == 0) {
					board[i][k] = board[i][j];
					board[i][j] = 0;
					if_need_add_num = 1;
				}
				else {
					board[i][--k] = board[i][j];
					if (j != k) {
						board[i][j] = 0;
						if_need_add_num = 1;
					}
				}
			}
		}
	}
}

/*上移 */
void move_up() {
	//仿照左移操作，区别仅仅是行列互换后遍历
	int i;
	for (i = 0; i < 4; ++i) {
		int j, k;
		for (j = 1, k = 0; j < 4; ++j) {
			if (board[j][i] > 0) {
				if (board[k][i] == board[j][i]) {
					score += board[k++][i] *= 2;
					board[j][i] = 0;
					if_need_add_num = 1;
				}
				else if (board[k][i] == 0) {
					board[k][i] = board[j][i];
					board[j][i] = 0;
					if_need_add_num = 1;
				}
				else {
					board[++k][i] = board[j][i];
					if (j != k) {
						board[j][i] = 0;
						if_need_add_num = 1;
					}
				}
			}
		}
	}
}

/*下移 */
void move_down() {
	//仿照左移操作，区别仅仅是行列互换后遍历，且j和k都反向遍历 
	int i;
	for (i = 0; i < 4; ++i) {
		int j, k;
		for (j = 2, k = 3; j >= 0; --j) {
			if (board[j][i] > 0) {
				if (board[k][i] == board[j][i]) {
					score += board[k--][i] *= 2;
					board[j][i] = 0;
					if_need_add_num = 1;
				}
				else if (board[k][i] == 0) {
					board[k][i] = board[j][i];
					board[j][i] = 0;
					if_need_add_num = 1;
				}
				else {
					board[--k][i] = board[j][i];
					if (j != k) {
						board[j][i] = 0;
						if_need_add_num = 1;
					}
				}
			}
		}
	}
}

/*刷新界面 */
void refresh_screen() {
	clear();
	printf("\n\n\n");
	printf("                WELCOME TO THE GAME OF 2048 IN ORANGE KING SYSTEM!\n");
	printf("                TIPS--W:UP S:DOWN A:LEFT D:RIGHT Q:EXIT---\n");
	printf("                YOUR SCORE: %5d\n", score);
	printf("\n\n                             +----+----+----+----+\n");
	int i;
	for (i = 0; i < 4; ++i) {
		printf("                             +");
		int j;
		//随着数字变大，棋盘格子也需要变大 
		for (j = 0; j < 4; ++j) {
			if (board[i][j] != 0) {
				if (board[i][j] < 10) {
					printf("  %d +", board[i][j]);
				}
				else if (board[i][j] < 100) {
					printf(" %d +", board[i][j]);
				}
				else if (board[i][j] < 1000) {
					printf(" %d+", board[i][j]);
				}
				else if (board[i][j] < 10000) {
					printf("%4d+", board[i][j]);
				}
				else {
					int n = board[i][j];
					int k;
					for (k = 1; k < 20; ++k) {
						n = n >> 1;
						if (n == 1) {
							printf("2^%02d+", k); //超过四位的数字用2的幂形式表示，如2^13形式
							break;
						}
					}
				}
			}
			else printf("    +");
		}

		if (i < 3) {
			printf("\n                             +----+----+----+----+\n");
		}
		else {
			printf("\n                             +----+----+----+----+\n");
		}
	}
	printf("\n");
	printf("                  Input your commond:");

	if (get_null_count() == 0) {
		check_game_over();
		//判断是否输了 
		if (if_game_over) {
			printf("\r                      \nGAME OVER! TRY AGAIN? [Y/N]:     \b\b\b\b");
		}
	}

	//判断是否退出游戏
	if (if_prepare_exit) {
		printf("\r                   \nDO YOU REALLY WANT TO QUIT THE GAME? [Y/N]:   \b\b");

	}
}
/*初始化游戏 */
void init_game() {
	reset_game();
}
/*释放游戏 */
void release_game(int signal) {
	clear();
	if (signal == SIGINT) {
		printf("\n");
	}

}
/*游戏主函数 */
void gameMain(char* option, int fd_stdin, int fd_stdout)
{
	if (!strcmp(option, "NULL")) {
		GAME2048(fd_stdin, fd_stdout);
	}
	else {
		printf("Sorry, there no such option for game.\n");
	}
	printf("\n");
}

/*系统级应用：文件模块 */

#define MAX_FILE_PER_LAYER 10
#define MAX_FILE_NAME_LENGTH 20
#define MAX_CONTENT_ 50
#define MAX_FILE_NUM 100

int fileIDCount = 0;//文件ID计数器
int currentFileID = 0;

/*定义文件描述符 */ 
struct fileBlock {
	int fileID;
	char fileName[MAX_FILE_NAME_LENGTH];
	int fileType; //0 for txt, 1 for folder
	char content[MAX_CONTENT_];
	int fatherID;
	int children[MAX_FILE_PER_LAYER];
	int childrenNumber;
};
struct fileBlock blocks[MAX_FILE_NUM];
int IDLog[MAX_FILE_NUM];

//文件管理主函数
void runFileManage(int fd_stdin) {
	char rdbuf[128];
	char cmd[8];
	char filename[120];
	char buf[1024];
	int m, n;
	char _name[MAX_FILE_NAME_LENGTH];
	FSInit();
	int len = ReadDisk();
	ShowMessage();
	while (1) {
		for (int i = 0; i <= 127; i++)
			rdbuf[i] = '\0';
		for (int i = 0; i < MAX_FILE_NAME_LENGTH; i++)
			_name[i] = '\0';
		printf("\n/%s:", blocks[currentFileID].fileName);

		int r = read(fd_stdin, rdbuf, 70);
		rdbuf[r] = 0;
		assert(fd_stdin == 0);

		char target[10];
		for (int i = 0; i <= 1 && i < r; i++) {
			target[i] = rdbuf[i];
		}
		//mkf指令定义make file 
		if (rdbuf[0] == 'm' && rdbuf[1] == 'k' && rdbuf[2] == 'f' ) {
			if (rdbuf[3] != ' ') {
				printf("You should add the filename, like \"create XXX\".\n");
				printf("Please input [help] to know more.\n");
				continue;
			}
			for (int i = 0; i < MAX_FILE_NAME_LENGTH && i < r - 3; i++) {
				_name[i] = rdbuf[i + 4];
			}
			CreateFIle(_name, 0);
		}
		//mkdir指令定义make directory 
		else if (rdbuf[0] == 'm' && rdbuf[1] == 'k' && rdbuf[2] == 'd' && rdbuf[3] == 'i' && rdbuf[4] == 'r') {
			if (rdbuf[5] != ' ') {
				printf("You should add the dirname, like \"mkdir XXX\".\n");
				printf("Please input [help] to know more.\n");
				continue;
			}
			char N[MAX_FILE_NAME_LENGTH];
			for (int i = 0; i < MAX_FILE_NAME_LENGTH && i < r - 3; i++) {
				_name[i] = rdbuf[i + 6];
			}
			CreateFIle(_name, 1);
		}
		//ls指令定义list 
		else if (rdbuf[0] == 'l' && rdbuf[1] == 's') {
			showFileList();
		}
		//cd指令定义change directory 
		else if (rdbuf[0] == 'c' && rdbuf[1] == 'd') {
			if (rdbuf[2] == ' ' && rdbuf[3] == '.' && rdbuf[4] == '.') {
				ReturnFile(currentFileID);
				continue;
			}
			else if (rdbuf[2] != ' ') {
				printf("You should add the dirname, like \"cd XXX\".\n");
				printf("Please input [help] to know more.\n");

				continue;
			}
			for (int i = 0; i < MAX_FILE_NAME_LENGTH && i < r - 3; i++) {
				_name[i] = rdbuf[i + 3];
			}
			printf("name: %s\n", _name);
			int ID = SearchFile(_name);
			if (ID >= 0) {
				if (blocks[ID].fileType == 1) {
					currentFileID = ID;
					continue;
				}
				else if (blocks[ID].fileType == 0) {
					while (1) {
						printf("input the character representing the method you want to operate:"
							"\nu --- update"
							"\nd --- detail of the content"
							"\nq --- quit\n");
						int r = read(fd_stdin, rdbuf, 70);
						rdbuf[r] = 0;
						if (strcmp(rdbuf, "u") == 0) {
							printf("input the text you want to write:\n");
							int r = read(fd_stdin, blocks[ID].content, MAX_CONTENT_);
							blocks[ID].content[r] = 0;
						}
						else if (strcmp(rdbuf, "d") == 0) {
							printf("--------------------------------------------"
								"\n%s\n-------------------------------------\n", blocks[ID].content);
						}
						else if (strcmp(rdbuf, "q") == 0) {
							printf("would you like to save the change? y/n");
							int r = read(fd_stdin, rdbuf, 70);
							rdbuf[r] = 0;
							if (strcmp(rdbuf, "y") == 0) {
								printf("save changes!");
							}
							else {
								printf("quit without changing");
							}
							break;
						}
					}
				}
			}
			else
				printf("No such file!");
		}
		//rm指令定义remove 
		else if (rdbuf[0] == 'r' && rdbuf[1] == 'm') {
			if (rdbuf[2] != ' ') {
				printf("You should add the filename or dirname, like \"rm XXX\".\n");
				printf("Please input [help] to know more.\n");
				continue;
			}
			for (int i = 0; i < MAX_FILE_NAME_LENGTH && i < r - 3; i++) {
				_name[i] = rdbuf[i + 3];
			}
			int ID = SearchFile(_name);
			if (ID >= 0) {
				printf("Delete successfully!\n");
				DeleteFile(ID);
				for (int i = 0; i < blocks[currentFileID].childrenNumber; i++) {
					if (ID == blocks[currentFileID].children[i]) {
						for (int j = i + 1; j < blocks[currentFileID].childrenNumber; j++) {
							blocks[currentFileID].children[i] = blocks[currentFileID].children[j];
						}
						blocks[currentFileID].childrenNumber--;
						break;
					}
				}
			}
			else
				printf("No such file!\n");
		}
		//sv指令定义save 
		else if (rdbuf[0] == 's' && rdbuf[1] == 'v') {
			WriteDisk(1000);
			printf("Save to disk successfully!\n");
		}
		//help指令定义 
		else if (strcmp(rdbuf, "help") == 0) {
			printf("\n");
			ShowMessage();
		}
		//quit指令定义 
		else if (strcmp(rdbuf, "quit") == 0) {
			clear();
			break;
		}
		//clear指令定义 
		else if (!strcmp(rdbuf, "clear")) {
			clear();
		}
		//其他指令 
		else {
			file_usage();
			printf("Sorry, there no such command in the File Manager.\n");
			printf("You can input [help] to know more.\n");
		}
	}

}
/*初始化文件描述符 */ 
void initFileBlock(int fileID, char* fileName, int fileType) {
	blocks[fileID].fileID = fileID;
	strcpy(blocks[fileID].fileName, fileName);
	blocks[fileID].fileType = fileType;
	blocks[fileID].fatherID = currentFileID;
	blocks[fileID].childrenNumber = 0;
}
/*转化为字符串 */ 
void toStr3(char* temp, int i) {
	if (i / 100 < 0)
		temp[0] = (char)48;
	else
		temp[0] = (char)(i / 100 + 48);
	if ((i % 100) / 10 < 0)
		temp[1] = '0';
	else
		temp[1] = (char)((i % 100) / 10 + 48);
	temp[2] = (char)(i % 10 + 48);
}
/*sv语句，写到硬盘 */ 
void WriteDisk(int len) {
	char temp[MAX_FILE_NUM * 150 + 103];
	int i = 0;
	temp[i] = '^';
	i++;
	toStr3(temp + i, fileIDCount);
	i = i + 3;
	temp[i] = '^';
	i++;
	for (int j = 0; j < MAX_FILE_NUM; j++) {
		if (IDLog[j] == 1) {
			toStr3(temp + i, blocks[j].fileID);
			i = i + 3;
			temp[i] = '^';
			i++;
			for (int h = 0; h < strlen(blocks[j].fileName); h++) {
				temp[i + h] = blocks[j].fileName[h];
				if (blocks[j].fileName[h] == '^')
					temp[i + h] = (char)1;
			}
			i = i + strlen(blocks[j].fileName);
			temp[i] = '^';
			i++;
			temp[i] = (char)(blocks[j].fileType + 48);
			i++;
			temp[i] = '^';
			i++;
			for (int h = 0; h < strlen(blocks[j].content); h++) {
				temp[i + h] = blocks[j].content[h];
				if (blocks[j].content[h] == '^')
					temp[i + h] = (char)1;
			}
			i = i + strlen(blocks[j].content);
			temp[i] = '^';
			i++;
			toStr3(temp + i, blocks[j].fatherID);
			i = i + 3;
			temp[i] = '^';
			i++;
			for (int m = 0; m < MAX_FILE_PER_LAYER; m++) {
				toStr3(temp + i, blocks[j].children[m]);
				i = i + 3;
			}
			temp[i] = '^';
			i++;
			toStr3(temp + i, blocks[j].childrenNumber);
			i = i + 3;
			temp[i] = '^';
			i++;
		}
	}
	int fd = 0;
	int n1 = 0;
	fd = open("ss", O_RDWR);
	assert(fd != -1);
	n1 = write(fd, temp, strlen(temp));
	assert(n1 == strlen(temp));
	close(fd);
}
/*转化为整型类型 */ 
int toInt(char* temp) {
	int result = 0;
	for (int i = 0; i < 3; i++)
		result = result * 10 + (int)temp[i] - 48;
	return result;
}
/*读硬盘 */ 
int ReadDisk() {
	char bufr[1000];
	int fd = 0;
	int n1 = 0;
	fd = open("ss", O_RDWR);
	assert(fd != -1);
	n1 = read(fd, bufr, 1000);
	assert(n1 == 1000);
	bufr[n1] = 0;
	int r = 1;
	fileIDCount = toInt(bufr + r);
	r = r + 4;
	for (int i = 0; i < fileIDCount; i++) {
		int ID = toInt(bufr + r);
		IDLog[ID] = 1;
		blocks[ID].fileID = ID;
		r = r + 4;
		for (int i = 0; i < MAX_FILE_NAME_LENGTH; i++) {
			if (bufr[r] == '^')
				break;
			else if (bufr[r] == (char)1)
				blocks[ID].fileName[i] = '^';
			else
				blocks[ID].fileName[i] = bufr[r];
			r++;
		}
		r++;
		blocks[ID].fileType = (int)bufr[r] - 48;
		r = r + 2;
		for (int j = 0; j < MAX_CONTENT_; j++) {
			if (bufr[r] == '^')
				break;
			else if (bufr[r] == (char)1)
				blocks[ID].content[j] = '^';
			else
				blocks[ID].content[j] = bufr[r];
			r++;
		}
		r++;
		blocks[ID].fatherID = toInt(bufr + r);
		r = r + 4;
		for (int j = 0; j < MAX_FILE_PER_LAYER; j++) {
			blocks[ID].children[j] = toInt(bufr + r);
			r = r + 3;
		}
		r++;
		blocks[ID].childrenNumber = toInt(bufr + r);
		r = r + 4;
	}
	return n1;
}
/*文件系统初始化 */ 
void FSInit() {

	for (int i = 0; i < MAX_FILE_NUM; i++) {
		blocks[i].childrenNumber = 0;
		blocks[i].fileID = -2;
		IDLog[i] = '\0';
	}
	IDLog[0] = 1;
	blocks[0].fileID = 0;
	strcpy(blocks[0].fileName, "home");
	strcpy(blocks[0].content, "welcome to use ORANGE KING's file system!");
	blocks[0].fileType = 2;
	blocks[0].fatherID = 0;
	blocks[0].childrenNumber = 0;
	currentFileID = 0;
	fileIDCount = 1;
}
/*创建文件函数 */ 
int CreateFIle(char* fileName, int fileType) {
	if (blocks[currentFileID].childrenNumber == MAX_FILE_PER_LAYER) {
		printf("Sorry you cannot add more files in this layer.\n");
		return 0;
	}
	else {
		for (int i = 0; i < blocks[currentFileID].childrenNumber; i++) {
			if (strcmp(blocks[blocks[currentFileID].children[i]].fileName, fileName) == 0) {
				if (fileType) {
					printf("You have a folder of same name!\n");
				}
				else {
					printf("You have a file of same name!\n");
				}
				return 0;
			}
		}
		fileIDCount++;
		int target = 0;
		for (int i = 0; i < MAX_FILE_NUM; i++) {
			if (IDLog[i] == 0) {
				target = i;
				break;
			}
		}
		initFileBlock(target, fileName, fileType);
		blocks[currentFileID].children[blocks[currentFileID].childrenNumber] = target;
		blocks[currentFileID].childrenNumber++;
		if (fileType) {
			printf("Create directory %s successful!\n", fileName);
		}
		else {
			printf("Create file %s successful!\n", fileName);
		}
		IDLog[target] = 1;
		return 1;
	}
}
/*展示文件列表 */ 
void showFileList() {
	printf("The elements in %s.\n", blocks[currentFileID].fileName);//通过currentFileID获取当前路径s

	printf("+----------+-----------+----------+\n");
	printf("| filename |    type   |    id    |\n");
	printf("+----------+-----------+----------+\n");
	for (int i = 0; i < blocks[currentFileID].childrenNumber; i++) {//遍历每个孩子
		printf("|%9s", blocks[blocks[currentFileID].children[i]].fileName);
		if (blocks[blocks[currentFileID].children[i]].fileType == 0) {
			printf(" | .txt file |");
		}
		else {
			printf(" |   folder  |");
		}
		printf("%10d|\n", blocks[blocks[currentFileID].children[i]].fileID);
	}
	printf("+----------+-----------+----------+\n");
}
/*搜索文件 */ 
int SearchFile(char* name) {
	for (int i = 0; i < blocks[currentFileID].childrenNumber; i++) {
		if (strcmp(name, blocks[blocks[currentFileID].children[i]].fileName) == 0) {
			return blocks[currentFileID].children[i];
		}
	}
	return -2;
}
/*返回文件的父节点，即返回上一级 */ 
void ReturnFile(int ID) {
	currentFileID = blocks[ID].fatherID;
}
/*删除文件 */ 
void DeleteFile(int ID) {
	if (blocks[ID].childrenNumber > 0) {
		for (int i = 0; i < blocks[ID].childrenNumber; i++) {
			DeleteFile(blocks[blocks[ID].children[i]].fileID);
		}
	}
	IDLog[ID] = 0;
	blocks[ID].fileID = -2;
	blocks[ID].childrenNumber = 0;
	for (int i = 0; i < MAX_CONTENT_; i++)
		blocks[ID].content[i] = '\0';
	for (int i = 0; i < MAX_FILE_NAME_LENGTH; i++)
		blocks[ID].fileName[i] = '\0';
	blocks[ID].fileType = -1;
	for (int i = 0; i < MAX_FILE_PER_LAYER; i++)
		blocks[ID].children[i] = -1;
	blocks[ID].fatherID = -2;
	fileIDCount--;
}

void file_usage()
{
	printf("/Test Usage/\n");
	printf("      +------------------------------------------------------------------+\n");
	printf("      |                OrangeKingOS---File Manager                       |\n");
	printf("      +-------------------------+----------------------------------------+\n");
	printf("      |     [COMMAND]           |   [FUNCTION]                           |\n");
	printf("      +-------------------------+----------------------------------------+\n");
	printf("      |     $ mkf <filename>    |   create a new txt file                |\n");
	printf("      |     $ mkdir <dirname>   |   create a new directory               |\n");
	printf("      |     $ ls                |   list the elements in this level      |\n");
	printf("      |     $ cd <dirname>      |   change directory to <dirname>        |\n");
	printf("      |     $ cd ..             |   return to the superior directory     |\n");
	printf("      |     $ rm <name>         |   delete a file or directory           |\n");
	printf("      |     $ help              |   show command list of this system     |\n");
	printf("      |     $ clear             |   clear the cmd                        |\n");
	printf("      |     $ quit              |   quit the File Manager                |\n");	
	printf("      +-------------------------+----------------------------------------+\n");
	printf("      |           Powered by AlphaWhiskyLou, LingWangzZ, hky011011       |\n");
	printf("      +------------------------------------------------------------------+\n");
	printf("\n\n");
}

void ShowMessage() {
	printf("      +------------------------------------------------------------------+\n");
	printf("      |                OrangeKingOS---File Manager                       |\n");
	printf("      +-------------------------+----------------------------------------+\n");
	printf("      |     [COMMAND]           |   [FUNCTION]                           |\n");
	printf("      +-------------------------+----------------------------------------+\n");
	printf("      |     $ mkf <filename>    |   create a new txt file                |\n");
	printf("      |     $ mkdir <dirname>   |   create a new directory               |\n");
	printf("      |     $ ls                |   list the elements in this level      |\n");
	printf("      |     $ cd <dirname>      |   change directory to <dirname>        |\n");
	printf("      |     $ cd ..             |   return to the superior directory     |\n");
	printf("      |     $ rm <name>         |   delete a file or directory           |\n");
	printf("      |     $ help              |   show command list of this system     |\n");
	printf("      |     $ clear             |   clear the cmd                        |\n");
	printf("      |     $ quit              |   quit the File Manager                |\n");	
	printf("      +-------------------------+----------------------------------------+\n");
	printf("      |           Powered by AlphaWhiskyLou, LingWangzZ, hky011011       |\n");
	printf("      +------------------------------------------------------------------+\n");
	printf("\n\n");
}


/*系统应用： 进程管理*/

/*进程管理主函数*/
void ProcessManagement(int fd_stdin)
{
	clear();
	char readbuffer[128];
	showProcessWelcome();
	while (1)
	{
		printf("OrangeKingOS  Process-Management: $ ");

		int end = read(fd_stdin, readbuffer, 70);
		readbuffer[end] = 0;
		int i = 0, j = 0;
		//获得命令指令
		char cmd[20] = { 0 };
		while (readbuffer[i] != ' ' && readbuffer[i] != 0)
		{
			cmd[i] = readbuffer[i];
			i++;
		}
		i++;
		//获取命令目标
		char target[20] = { 0 };
		while (readbuffer[i] != ' ' && readbuffer[i] != 0)
		{
			target[j] = readbuffer[i];
			i++;
			j++;
		}
		//结束进程;
		if (strcmp(cmd, "kill") == 0)
		{
			killProcess(target);
			continue;
		}
		//重启进程
		else if (strcmp(cmd, "restart") == 0)
		{
			restartProcess(target);
			continue;
		}
		//打印全部进程
		else if (strcmp(readbuffer, "ps") == 0)
		{
			showProcess();
		}
		//退出进程管理
		else if (strcmp(readbuffer, "quit") == 0)
		{
			clear();

			break;
		}
		else if (!strcmp(readbuffer, "clear")) {
			clear();
		}
		//错误命令提示
		else
		{
			process_usage();
			printf("Invalid Command \n");
			printf("\n");
		}
	}
}


/*打印欢迎界面*/
void showProcessWelcome()
{
	printf("      +------------------------------------------------------------------+\n");
	printf("      |                            Welcome to                            |\n");
	printf("      |                  OrangeKingOS  Process Manager                   |\n");
	printf("      |------------------------------------------------------------------|\n");
	printf("      |             [COMMAND]                 [FUNCTION]                 |\n");
	printf("      |                                                                  |\n");
	printf("      |           $ ps           |     show all process                  |\n");
	printf("      |           $ kill <id>    |     kill a process                    |\n");
	printf("      |           $ restart <id> |     restart a process                 |\n");
	printf("      |           $ quit         |     quit process management           |\n");
	printf("      |           $ help         |     show command list                 |\n");
	printf("      |           $ clear        |     clear the cmd                     |\n");
	printf("      |------------------------------------------------------------------|\n");
	printf("      |                                                                  |\n");
	printf("      |          Powered by AlphaWhiskyLou, LingWangzZ, hky011011        |\n");
	printf("      |                                                                  |\n");
	printf("      +------------------------------------------------------------------+\n");

	printf("\n\n");
}

/*打印所有进程*/
void showProcess()
{
	int i;
	printf("===============================================================================\n");
	printf("    ProcessID    |    ProcessName    |    ProcessPriority    |    Running          \n");
	//进程号，进程名，优先级，是否在运行
	printf("-------------------------------------------------------------------------------\n");
	for (i = 0; i < NR_TASKS + NR_PROCS; i++)//逐个遍历
	{
		printf("        %d", proc_table[i].pid);
		printf("                 %5s", proc_table[i].name);
		printf("                   %2d", proc_table[i].priority);
		if (proc_table[i].priority == 0)
		{
			printf("                   no\n");
		}
		else
		{
			printf("                   yes\n");
		}
		//printf("        %d                 %s                   %d                   yes\n", proc_table[i].pid, proc_table[i].name, proc_table[i].priority);
	}
	printf("===============================================================================\n\n");
}

/*计算编号*/
int getMag(int n)
{
	int mag = 1;
	for (int i = 0; i < n; i++)
	{
		mag = mag * 10;
	}
	return mag;
}

/*计算进程pid*/
int getPid(char str[])
{
	int length = 0;
	for (; length < MAX_FILENAME_LEN; length++)
	{
		if (str[length] == '\0')
		{
			break;
		}
	}
	int pid = 0;
	for (int i = 0; i < length; i++)
	{
		if (str[i] - '0' > -1 && str[i] - '9' < 1)
		{
			pid = pid + (str[i] + 1 - '1') * getMag(length - 1 - i);
		}
		else
		{
			pid = -1;
			break;
		}
	}
	return pid;
}

/*结束进程*/
void killProcess(char str[])
{
	int pid = getPid(str);

	//健壮性处理以及结束进程
	if (pid >= NR_TASKS + NR_PROCS || pid < 0)
	{
		printf("The pid exceeded the range\n");
	}
	else if (pid < NR_TASKS)
	{
		printf("System tasks cannot be killed.\n");
	}
	else if (proc_table[pid].priority == 0 || proc_table[pid].p_flags == -1)
	{
		printf("Process not found.\n");
	}
	else if (pid == 4 || pid == 6)
	{
		printf("This process cannot be killed.\n");
	}
	else
	{
		proc_table[pid].priority = 0;
		proc_table[pid].p_flags = -1;
		printf("Target process killed.\n");
	}

	showProcess();
}

/*重启进程*/
void restartProcess(char str[])
{
	int pid = getPid(str);

	if (pid >= NR_TASKS + NR_PROCS || pid < 0)
	{
		printf("The pid exceeded the range\n");
	}
	else if (proc_table[pid].p_flags != -1)
	{
		printf("This process is already running.\n");
	}
	else
	{
		proc_table[pid].priority = 1;
		proc_table[pid].p_flags = 1;
		printf("Target process is running.\n");
	}

	showProcess();
}

/*进程管理功能提示*/
void process_usage()
{
	printf("      |------------------------------------------------------------------|\n");
	printf("      |             [COMMAND]                 [FUNCTION]                 |\n");
	printf("      |                                                                  |\n");
	printf("      |           $ ps           |     show all process                  |\n");
	printf("      |           $ kill <id>    |     kill a process                    |\n");
	printf("      |           $ restart <id> |     restart a process                 |\n");
	printf("      |           $ quit         |     quit process management           |\n");
	printf("      |           $ help         |     show command list                 |\n");
	printf("      |           $ clear        |     clear the cmd                     |\n");
	printf("      |------------------------------------------------------------------|\n");
}


/* 内核启动模块 */
PUBLIC int kernel_main()
{
	disp_str("-----\"kernel_main\" begins-----\n");

	struct task* p_task;
	struct proc* p_proc = proc_table;
	char* p_task_stack = task_stack + STACK_SIZE_TOTAL;
	u16   selector_ldt = SELECTOR_LDT_FIRST;
	u8    privilege;
	u8    rpl;
	int   eflags;
	int   i, j;
	int   prio;
	for (i = 0; i < NR_TASKS + NR_PROCS; i++) {
		if (i < NR_TASKS) {     /* 任务 */
			p_task = task_table + i;
			privilege = PRIVILEGE_TASK;
			rpl = RPL_TASK;
			eflags = 0x1202; /* IF=1, IOPL=1, bit 2 is always 1 */
			prio = 15;
		}
		else {                  /* 用户进程 */
			p_task = user_proc_table + (i - NR_TASKS);
			privilege = PRIVILEGE_USER;
			rpl = RPL_USER;
			eflags = 0x202; /* IF=1, bit 2 is always 1 */
			prio = 5;
		}

		strcpy(p_proc->name, p_task->name);	/* name of the process */
		p_proc->pid = i;			/* pid */

		p_proc->ldt_sel = selector_ldt;

		memcpy(&p_proc->ldts[0], &gdt[SELECTOR_KERNEL_CS >> 3],
			sizeof(struct descriptor));
		p_proc->ldts[0].attr1 = DA_C | privilege << 5;
		memcpy(&p_proc->ldts[1], &gdt[SELECTOR_KERNEL_DS >> 3],
			sizeof(struct descriptor));
		p_proc->ldts[1].attr1 = DA_DRW | privilege << 5;
		p_proc->regs.cs = (0 & SA_RPL_MASK & SA_TI_MASK) | SA_TIL | rpl;
		p_proc->regs.ds = (8 & SA_RPL_MASK & SA_TI_MASK) | SA_TIL | rpl;
		p_proc->regs.es = (8 & SA_RPL_MASK & SA_TI_MASK) | SA_TIL | rpl;
		p_proc->regs.fs = (8 & SA_RPL_MASK & SA_TI_MASK) | SA_TIL | rpl;
		p_proc->regs.ss = (8 & SA_RPL_MASK & SA_TI_MASK) | SA_TIL | rpl;
		p_proc->regs.gs = (SELECTOR_KERNEL_GS & SA_RPL_MASK) | rpl;

		p_proc->regs.eip = (u32)p_task->initial_eip;
		p_proc->regs.esp = (u32)p_task_stack;
		p_proc->regs.eflags = eflags;

		/* p_proc->nr_tty		= 0; */

		p_proc->p_flags = 0;
		p_proc->p_msg = 0;
		p_proc->p_recvfrom = NO_TASK;
		p_proc->p_sendto = NO_TASK;
		p_proc->has_int_msg = 0;
		p_proc->q_sending = 0;
		p_proc->next_sending = 0;

		for (j = 0; j < NR_FILES; j++)
			p_proc->filp[j] = 0;

		p_proc->ticks = p_proc->priority = prio;

		p_task_stack -= p_task->stacksize;
		p_proc++;
		p_task++;
		selector_ldt += 1 << 3;
	}

	k_reenter = 0;
	ticks = 0;

	p_proc_ready = proc_table;

	init_clock();
	init_keyboard();

	restart();

	while (1) {}
}

PUBLIC int get_ticks()
{
	MESSAGE msg;
	reset_msg(&msg);
	msg.type = GET_TICKS;
	send_recv(BOTH, TASK_SYS, &msg);
	return msg.RETVAL;
}

void TestA()
{
	int fd;
	int i, n;

	char tty_name[] = "/dev_tty0";

	char rdbuf[128];
	char command3[100], command4[100], command5[100];

	int fd_stdin = open(tty_name, O_RDWR);
	assert(fd_stdin == 0);
	int fd_stdout = open(tty_name, O_RDWR);
	assert(fd_stdout == 1);

	//	char filename[MAX_FILENAME_LEN+1] = "zsp01";
	const char bufw[80] = { 0 };

	clear();
	Booting();
	Loading();
	clear();

	CommandList();

	while (1) {
		printf("usr@OrangeKingOS: $ ");

		memset(command3, 0, sizeof(command3));
		memset(command4, 0, sizeof(command4));
		memset(command5, 0, sizeof(command5));

		int r = read(fd_stdin, rdbuf, 70);
		rdbuf[r] = 0;
		mystrncpy(command3, rdbuf, 3);
		mystrncpy(command4, rdbuf, 4);
		mystrncpy(command5, rdbuf, 5);

		if (!strcmp(command4, "help"))
		{
			clear();
			CommandList();
		}
		else if (!strcmp(command5, "clear"))
		{
			clear();
		}
		else if (!strcmp(command4, "game"))
		{
			if (strlen(rdbuf) > 5) {
				gameMain(rdbuf + 5, fd_stdin, fd_stdout);
			}
			else {
				char* str = "NULL";
				gameMain(str, fd_stdin, fd_stdout);
			}
			continue;
		}
		else if (!strcmp(command4, "calc"))
		{
			if (strlen(rdbuf) > 5) {
				calcMain(rdbuf + 5);
			}
			else {
				char* str = "NULL";
				calcMain(str);
			}

			continue;
		}

		else if (!strcmp(command3, "cal"))
		{
			if (strlen(rdbuf) > 4) {
				calMain(rdbuf + 4);
			}
			else {
				char* str = "NULL";
				calMain(str);
			}
			continue;
		}
		else if (!strcmp(rdbuf, "process")) {
			clear();
			ProcessManagement(fd_stdin);
		}
		else if (!strcmp(rdbuf, "file")) {
			clear();
			runFileManage(fd_stdin);
		}
		else if (!strcmp(rdbuf, ""))
		{
			continue;
		}
		else
		{
			clear();
			NotFound();
		}

	}
}

void TestB()
{
	char tty_name[] = "/dev_tty1";
	int fd_stdin = open(tty_name, O_RDWR);
	assert(fd_stdin == 0);
	int fd_stdout = open(tty_name, O_RDWR);
	assert(fd_stdout == 1);
	runFileManage(fd_stdin); 
}

void TestC()
{
	spin("TestC");
}

PUBLIC void panic(const char* fmt, ...)
{
	int i;
	char buf[256];

	/* 4 is the size of fmt in the stack */
	va_list arg = (va_list)((char*)& fmt + 4);

	i = vsprintf(buf, fmt, arg);

	printl("%c !!panic!! %s", MAG_CH_PANIC, buf);

	/* should never arrive here */
	__asm__ __volatile__("ud2");
}

/* 系统应用：控制台命令行 */

/* 清屏函数 */
void clear()
{
	/* 清屏 */
	clear_screen(0, console_table[current_console].cursor);
	/* 初始化 */
	console_table[current_console].crtc_start = 0;
	console_table[current_console].cursor = 0;
}


void mystrncpy(char* dest, char* src, int len)
{
	assert(dest != NULL && src != NULL);

	char* temp = dest;
	int i = 0;
	while (i++ < len && (*temp++ = *src++) != '\0');

	if (*(temp) != '\0') {
		*temp = '\0';
	}
}

/*开机动画*/
void Booting()
{
	emptyWindow();
	gradualStart();
}
void emptyWindow()
{
	printf("      +------------------------------------------------------------------+\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      +------------------------------------------------------------------+\n");
	milli_delay(DELAY_TIME);
	clear();
}
void gradualStart()
{
	printf("      +------------------------------------------------------------------+\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                            @@@@@@@@@                             |\n");
	printf("      |                        @@@@@@@@@@@@@@@@@                         |\n");
	printf("      |                      @@@@@@@@@@@@@@@@@@@@@                       |\n");
	printf("      |                    @@@@@@@@@@@@@@@@@@@@@@@@@                     |\n");
	printf("      |                    @@@@@@@@@@@@@@@@@@@@@@@@@                     |\n");
	printf("      |                  @@@@@@@@@@@@@@@@@@@@@@@@@@@@@                   |\n");
	printf("      |                  @@@@@@@@@@@@@@@@@@@@@@@@@@@@@                   |\n");
	printf("      |                  @@@@@@@@@@@@@@@@@@@@@@@@@@@@@                   |\n");
	printf("      |                  @@@@@@@@@@@@@@@@@@@@@@@@@@@@@                   |\n");
	printf("      |                  @@@@@@@@@@@@@@@@@@@@@@@@@@@@@                   |\n");
	printf("      |                    @@@@@@@@@@@@@@@@@@@@@@@@@                     |\n");
	printf("      |                    @@@@@@@@@@@@@@@@@@@@@@@@@                     |\n");
	printf("      |                      @@@@@@@@@@@@@@@@@@@@@                       |\n");
	printf("      |                        @@@@@@@@@@@@@@@@@                         |\n");
	printf("      |                            @@@@@@@@@                             |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      +------------------------------------------------------------------+\n");
	milli_delay(DELAY_TIME);
	clear();

	milli_delay(DELAY_TIME);
	clear();

	printf("      +------------------------------------------------------------------+\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                            @@@@@@@@@                             |\n");
	printf("      |                        @@@@@@@@@@@@@@@@@                         |\n");
	printf("      |                      @@@@@@@@@@@@@@@@@@@@@                       |\n");
	printf("      |                    @@@@@@@@@@@@@@@@@@@@@@@@@                     |\n");
	printf("      |                    @@@@@@@@@@**@**@@@@@@@@@@                     |\n");
	printf("      |                  @@@@@@@@@@*@**@**@*@@@@@@@@@@                   |\n");
	printf("      |                  @@@@@@@@*****@@@*****@@@@@@@@                   |\n");
	printf("      |                  @@@@@@@@@@@@@@@@@@@@@@@@@@@@@                   |\n");
	printf("      |                  @@@@@@@@*****@@@*****@@@@@@@@                   |\n");
	printf("      |                  @@@@@@@@@@*@**@**@*@@@@@@@@@@                   |\n");
	printf("      |                    @@@@@@@@@@**@**@@@@@@@@@@                     |\n");
	printf("      |                    @@@@@@@@@@@@@@@@@@@@@@@@@                     |\n");
	printf("      |                      @@@@@@@@@@@@@@@@@@@@@                       |\n");
	printf("      |                        @@@@@@@@@@@@@@@@@                         |\n");
	printf("      |                            @@@@@@@@@                             |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      +------------------------------------------------------------------+\n");
	milli_delay(DELAY_TIME);
	clear();

	milli_delay(DELAY_TIME);
	clear();

	printf("      +------------------------------------------------------------------+\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                            @@@@@@@@@                             |\n");
	printf("      |                        @@@@@@@@@@@@@@@@@                         |\n");
	printf("      |                      @@@@@@@@@@@@@@@@@@@@@                       |\n");
	printf("      |                    @@@@@@@@@@**@**@@@@@@@@@@                     |\n");
	printf("      |                    @@@@@@@@****@****@@@@@@@@                     |\n");
	printf("      |                  @@@@@@@@***@**@**@***@@@@@@@@                   |\n");
	printf("      |                  @@@@@@*******@@@*******@@@@@@                   |\n");
	printf("      |                  @@@@@@@@@@@@@@@@@@@@@@@@@@@@@                   |\n");
	printf("      |                  @@@@@@*******@@@*******@@@@@@                   |\n");
	printf("      |                  @@@@@@@@***@**@**@***@@@@@@@@                   |\n");
	printf("      |                    @@@@@@@@****@****@@@@@@@@                     |\n");
	printf("      |                    @@@@@@@@@@**@**@@@@@@@@@@                     |\n");
	printf("      |                      @@@@@@@@@@@@@@@@@@@@@                       |\n");
	printf("      |                        @@@@@@@@@@@@@@@@@                         |\n");
	printf("      |                            @@@@@@@@@                             |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      +------------------------------------------------------------------+\n");
	milli_delay(DELAY_TIME);
	clear();
	milli_delay(DELAY_TIME);
	clear();

	printf("      +------------------------------------------------------------------+\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                            @@@@@@@@@                             |\n");
	printf("      |                        @@@@@@@@@@@@@@@@@                         |\n");
	printf("      |                      @@@@@@@@**@**@@@@@@@@                       |\n");
	printf("      |                    @@@@@@@@****@****@@@@@@@@                     |\n");
	printf("      |                    @@@@@@*@****@****@*@@@@@@                     |\n");
	printf("      |                  @@@@@@*****@**@**@*****@@@@@@                   |\n");
	printf("      |                  @@@@*********@@@*********@@@@                   |\n");
	printf("      |                  @@@@@@@@@@@@@@@@@@@@@@@@@@@@@                   |\n");
	printf("      |                  @@@@*********@@@*********@@@@                   |\n");
	printf("      |                  @@@@@@*****@**@**@*****@@@@@@                   |\n");
	printf("      |                    @@@@@@*@****@****@*@@@@@@                     |\n");
	printf("      |                    @@@@@@@@****@****@@@@@@@@                     |\n");
	printf("      |                      @@@@@@@@**@**@@@@@@@@                       |\n");
	printf("      |                        @@@@@@@@@@@@@@@@@                         |\n");
	printf("      |                            @@@@@@@@@                             |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      +------------------------------------------------------------------+\n");
	milli_delay(DELAY_TIME);
	clear();

	milli_delay(DELAY_TIME);
	clear();

	printf("      +------------------------------------------------------------------+\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                            @@@@@@@@@                             |\n");
	printf("      |                        @@@@@@**@**@@@@@@                         |\n");
	printf("      |                      @@@@@@****@****@@@@@@                       |\n");
	printf("      |                    @@@@@@******@******@@@@@@                     |\n");
	printf("      |                    @@@@***@****@****@***@@@@                     |\n");
	printf("      |                  @@@@*******@**@**@*******@@@@                   |\n");
	printf("      |                  @@***********@@@***********@@                   |\n");
	printf("      |                  @@@@@@@@@@@@@@@@@@@@@@@@@@@@@                   |\n");
	printf("      |                  @@***********@@@***********@@                   |\n");
	printf("      |                  @@@@*******@**@**@*******@@@@                   |\n");
	printf("      |                    @@@@***@****@****@***@@@@                     |\n");
	printf("      |                    @@@@@@******@******@@@@@@                     |\n");
	printf("      |                      @@@@@@****@****@@@@@@                       |\n");
	printf("      |                        @@@@@@**@**@@@@@@                         |\n");
	printf("      |                            @@@@@@@@@                             |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      +------------------------------------------------------------------+\n");
	milli_delay(DELAY_TIME);
	clear();

	milli_delay(DELAY_TIME);
	clear();

	printf("      +------------------------------------------------------------------+\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      |                            @@@@@@@@@                             |\n");
	printf("      |                        @@@@****@****@@@@                         |\n");
	printf("      |                      @@********@********@@                       |\n");
	printf("      |                    @@***@******@******@***@@                     |\n");
	printf("      |                    @@*****@****@****@*****@@                     |\n");
	printf("      |                  @@*********@**@**@*********@@                   |\n");
	printf("      |                  @@***********@@@***********@@                   |\n");
	printf("      |                  @@@@@@@@@@@@@@@@@@@@@@@@@@@@@                   |\n");
	printf("      |                  @@***********@@@***********@@                   |\n");
	printf("      |                  @@*********@**@**@*********@@                   |\n");
	printf("      |                    @@*****@****@****@*****@@                     |\n");
	printf("      |                    @@***@******@******@***@@                     |\n");
	printf("      |                      @@********@********@@                       |\n");
	printf("      |                        @@@@****@****@@@@                         |\n");
	printf("      |                            @@@@@@@@@                             |\n");
	printf("      |                                                                  |\n");
	printf("      |                                                                  |\n");
	printf("      +------------------------------------------------------------------+\n");
	milli_delay(DELAY_TIME);
	clear();
}
void Loading() {
	printf("System is loading...");

	for (int i = 0; i < 25; ++i)
	{
		milli_delay(DELAY_TIME / 5);
		printf(".");
	}
}

/*所有指令 & help窗口*/
void CommandList()
{
	printf("      +------------------------------------------------------------------+\n");
	printf("      |                                         Welcome to               |\n");
	printf("      |      *********                         OrangeKingOS              |\n");
	printf("      |     * *    *  *                                                  |\n");
	printf("      |     * *  *    *                          [COMMAND LIST]          |\n");
	printf("      |     * *       *             $ help --- show the command list     |\n");
	printf("      |     * * *     *             $ clear --- clear the cmd            |\n");
	printf("      |     * *   *   *                                                  |\n");
	printf("      |     * *     * *                                                  |\n");
	printf("      |      *********                                                   |\n");
	printf("      |                             $ game   play the game [2048]        |\n");
	printf("      |                             $ calc --<option> <expression>       |\n");
	printf("      |                                        calculate the value       |\n");
	printf("      |                             $ cal --<option>                     |\n");
	printf("      |                                        date information          |\n");
	printf("      |                             $ process  Process Management        |\n");
	printf("      |                             $ file     File Management           |\n");
	printf("      |                                                                  |\n");
	printf("      +------------------------------------------------------------------+\n");
	printf("      |           Powered by AlphaWhiskyLou, LingWangzZ, hky011011       |\n");
	printf("      +------------------------------------------------------------------+\n");


	printf("\n\n");
}

/*没找到该指令窗口*/
void NotFound()
{
	printf("      +------------------------------------------------------------------+\n");
	printf("      |                                                                  |\n");
	printf("      |      *********                  Command not found!               |\n");
	printf("      |     * *    *  *           You can refer to the command list!     |\n");
	printf("      |     * *  *    *                  [COMMAND LIST]                  |\n");
	printf("      |     * *       *             $ help --- show the command list     |\n");
	printf("      |     * * *     *             $ clear --- clear the cmd            |\n");
	printf("      |     * *   *   *                                                  |\n");
	printf("      |     * *     * *                                                  |\n");
	printf("      |      *********                                                   |\n");
	printf("      |                             $ game   play the game [2048]        |\n");
	printf("      |                             $ calc --<option> <expression>       |\n");
	printf("      |                                        calculate the value       |\n");
	printf("      |                             $ cal --<option>                     |\n");
	printf("      |                                        date information          |\n");
	printf("      |                             $ process  Process Management        |\n");
	printf("      |                             $ file     File Management           |\n");
	printf("      |                                                                  |\n");
	printf("      +------------------------------------------------------------------+\n");
	printf("      |           Powered by AlphaWhiskyLou, LingWangzZ, hky011011       |\n");
	printf("      +------------------------------------------------------------------+\n");
	printf("\n\n");
}

/* 用户应用：日历模块 */

/* 计算当天是周几 */
int zeller(int y, int m, int d) {
	int w, c, yh, i, wx;

	switch (m) {
		case 1:case 2:
			y = y - 1;
			m = m + 12;
			c = y / 100;
			yh = y % 100;
			w = yh + yh / 4 + c / 4 - 2 * c + 13 * (m + 1) / 5 + d - 1;
			break;
		default:
			c = y / 100;
			yh = y % 100;
			w = yh + yh / 4 + c / 4 - 2 * c + 13 * (m + 1) / 5 + d - 1;
			break;
	}

	if (w < 0) {
		for (i = 0; w < 0; i++) {
			w = w + 7;
		}
	}

	wx = w % 7;

	return(wx);
}

/* 判断闰年 */
int leap(int y) {
	if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) {
		return 1;
	}
	else {
		return 0;
	}
}

/* 功能提示 */
void cal_usage()
{
	printf("[OPTION LIST]  \n");
	printf("--year <year> -> output current year calendar \n");
	printf("--month <year/month> -> output current month calendar \n");
	printf("--day <year/month/day> -> output the position of current day \n");
	printf("[EXAMPLE]    \n");
	printf("cal --year 2021 \n");
	printf("cal --month 2021/8 \n");
	printf("cal --month 2021/8/4 \n");
}

/* 打印年历 */
void year(int y) {
	int m, d, p1, i, p, sum = 0, s, mp;
	int a[23] = { 0 }, b[13] = { 0,31,28,31,30,31,30,31,31,30,31,30,31 };
	b[2] = 28 + leap(y);

	printf("Calendar for %d : \n\n", y);

	for (s = 0; s <= 3; s++) {
		switch (s) {
			case 0:
				printf("        Jan                     Feb                     Mar\n");
				break;
			case 1:
				printf("        Apr                     May                     Jun\n");
				break;
			case 2:
				printf("        Jul                     Aug                     Sept\n");
				break;
			case 3:
				printf("        Oct                     Nov                     Dec\n");
				break;
			default:
				;

		}
		printf("Su Mo Tu We Th Fr Sa    Su Mo Tu We Th Fr Sa    Su Mo Tu We Th Fr Sa\n");

		for (m = 3 * s + 1; m <= 3 * s + 3; m++) {
			p1 = zeller(y, m, 1) + 8 * (m - 3 * s - 1);
			for (d = 1; p1 % 8 - 1 + d <= 6; d++) {
				a[p1 - 1 + d] = d;
			}
		}
		for (i = 0; i < 23; i++) {
			if (a[i] == 0) {
				printf("   ");
			}
			else {
				if (a[i] < 10) {
					printf("%d  ", a[i]);
				}
				else {
					printf("%d ", a[i]);
				}
			}
		}
		printf("\n");

		while (1) {
			sum = 0;
			for (mp = 1; mp <= 3; mp++) {
				if (a[8 * (mp - 1) + 6] == 0 || a[8 * (mp - 1) + 6] == b[mp + 3 * s]) {
					for (p = 8 * (mp - 1); p <= 8 * (mp - 1) + 6; p++) {
						a[p] = 0;
					}
				}
				else {
					a[8 * (mp - 1)] = a[8 * (mp - 1) + 6] + 1;
					for (p = 8 * (mp - 1); p <= 8 * (mp - 1) + 6; p++) {
						a[p] = p - 8 * (mp - 1) + a[8 * (mp - 1)];
						if (a[p] > b[mp + 3 * s]) {
							a[p] = 0;
						}
					}
				}
			}

			for (i = 0; i < 23; i++) {
				sum = sum + a[i];
			}
			if (sum == 0) {
				printf("\n");
				break;
			}

			for (i = 0; i < 23; i++) {

				if (a[i] == 0) {
					printf("   ");
				}
				else {
					if (a[i] < 10) {
						printf("%d  ", a[i]);
					}
					else {
						printf("%d ", a[i]);
					}
				}
			}
			printf("\n");


		}
	}
	printf("\n");
}

/* 打印月历 */
void month(int y, int m) {
	int d, w;
	int a[23] = { 0 }, b[13] = { 0,31,28,31,30,31,30,31,31,30,31,30,31 };
	b[2] = 28 + leap(y);
	w = zeller(y, m, 1);
	
	printf("Calendar for %d/%d : \n\n", y, m);
	printf("Sun Mon Tue Wed Thu Fri Sat\n");
	for (int i = 0; i < w; i++) {
		printf("    ");
	}
	
	for (d = 1; d <= b[m]; d++) {
		if (d < 10) {
			printf("%d   ", d);
		}
		else {
			printf("%d  ", d);
		}
		if ((d + w) % 7 == 0) {
			printf("\n");
		}
	}
	
	printf("\n\n");
}

/* 给出该日期所在的位置 */
void day(int y, int m, int d) {
	int count = 0, w;
	int a[23] = { 0 }, b[13] = { 0,31,28,31,30,31,30,31,31,30,31,30,31 };
	b[2] = 28 + leap(y);
	for (int i = 0; i < m; i++) {
		count += b[i];
	}
	count += d;
	w = zeller(y, m, d);

	printf("%d/%d/%d is day No.%d of the year.\n", y, m, d, count);
	printf("%d/%d/%d is ", y, m, d);

	switch (w) {
		case 0:
			printf("Sunday.\n");
			break;
		case 1:
			printf("Monday.\n");
			break;
		case 2:
			printf("Tuesday.\n");
			break;
		case 3:
			printf("Wedensday.\n");
			break;
		case 4:
			printf("Thursday.\n");
			break;
		case 5:
			printf("Friday.\n");
			break;
		case 6:
			printf("Saturday.\n");
			break;
		default:
			;
	}
}

/* 计算器主函数 */
void calMain(char* option)
{    
	int b[13] = { 0,31,28,31,30,31,30,31,31,30,31,30,31 };
	/* 计算器命令行判断 */
	char str_list[3][10] = { "--year","--month","--day" };
	int flag[3] = { 1,1,1 };
	for (int i = 0; i < 3; ++i) {
		int j = 0;
		while (option[j] != ' ' && option[j] != '\0') {
			if (option[j] != str_list[i][j]) {
				flag[i] = 0;
				break;
			}
			++j;
		}
	}

	
	int y, m, d;
	char year_str[5] = "\0", month_str[3] = "\0", day_str[3] = "\0";

	if (!strcmp(option, "NULL")) {
		cal_usage();
	}
	else if (flag[0]) {				//--year
		if (strlen(option) > 7) {
			char* value = option + 7;

			int i = 0;
			for (int j = 0; i < strlen(value) && value[i] != '/' && value[i] != ' '; ++i, ++j) {
				year_str[i] = value[i];
			}

			atoi(year_str, &y);

			if (y > 0) {
				year(y);
			}
			else {
				if (y < 0) {
					printf("ERROR:<year> should be greater than 0.\n");
				}
				printf("Please input again.\n");
			}
		}
		else {
			printf("Sorry, you should add [year].\n");
			printf("You can input [cal] to know more.\n");
		}
	}
	else if (flag[1]) {				//--month
		if (strlen(option) > 8) {
			char* value = option + 8;

			int i = 0;
			for (int j = 0; i < strlen(value) && value[i] != '/' && value[i] != ' '; ++i, ++j) {
				year_str[i] = value[i];
			}
			++i;
			for (int j = 0; i < strlen(value) && value[i] != '/' && value[i] != ' '; ++i, ++j) {
				month_str[j] = value[i];
			}

			atoi(year_str, &y);
			atoi(month_str, &m);

			if (y > 0 && m > 0 && m < 13) {
				month(y, m);
			}
			else {
				if (y < 0) {
					printf("ERROR:<year> should be greater than 0.\n");
				}
				if (m < 1 || m > 12) {
					printf("ERROR:<month> should be between 1 to 12.\n");
				}
				printf("Please input again.\n");
			}
		}
		else {
			printf("Sorry, you should add [year/month].\n");
			printf("You can input <cal> to know more.\n");
		}
	}
	else if (flag[2]) {				//--day
		if (strlen(option) > 6) {
			char* value = option + 6;

			int i = 0;
			for (int j = 0; i < strlen(value) && value[i] != '/' && value[i] != ' '; ++i, ++j) {
				year_str[i] = value[i];
			}
			++i;
			for (int j = 0; i < strlen(value) && value[i] != '/' && value[i] != ' '; ++i, ++j) {
				month_str[j] = value[i];
			}
			++i;
			for (int j = 0; i < strlen(value) && value[i] != ' '; ++i, ++j) {
				day_str[j] = value[i];
			}

			atoi(year_str, &y);
			atoi(month_str, &m);
			atoi(day_str, &d);

			if (y < 0) {
				printf("ERROR:<year> should be greater than 0.\n");
				printf("Please input again.\n");
			}
			else if (m < 1 || m > 12) {
				printf("ERROR:<month> should between 1 to 12.\n");
				printf("Please input again.\n");
			}
			else if (d < 1 || d > b[m] + leap(y)) {
				printf("ERROR:<day> should be legal.\n");
				printf("Please input again.\n");
			}
			else{
				day(y, m, d);
			}
		}
		else {
			printf("Sorry, you should add [year/month/day].\n");
			printf("You can input [cal] to know more.\n");
		}
	}
	else {
		printf("Sorry, there no such option for cal.\n");
		printf("You can input <cal> to know more.\n");
	}

	printf("\n");

	return 0;
}

/*用户应用： 计算器模块*/
typedef int bool;
typedef int DATA;
#define False 0
#define True 1
#define EMPTY_CH '\0'
#define EMPTY_NUM -999999
#define SIZE 50

/*判断表达式括号是否匹配 */
char bucket_stack[100] = "\0";
int bucket_stack_index = -1;

bool isempty_bucket_stack() {
	return bucket_stack_index == -1;
}

void bucket_stack_push(char ch) {
	bucket_stack_index++;
	bucket_stack[bucket_stack_index] = ch;
}

char bucket_stack_pop(void) {
	if (isempty_bucket_stack()) {
		return EMPTY_CH;
	}
	char ch = bucket_stack[bucket_stack_index];
	bucket_stack_index--;
	return ch;
}

void bucket_stack_clear(void) {
	memset(bucket_stack, '\0', sizeof(bucket_stack));
	bucket_stack_index = -1;
}


/*操作数栈*/
int num_stack[100] = { 0 };
int num_stack_index = -1;

bool isempty_num_stack() {
	return num_stack_index == -1;
}

void num_stack_push(int num) {
	num_stack_index++;
	num_stack[num_stack_index] = num;
}

int num_stack_pop(void) {
	if (isempty_num_stack()) {
		return EMPTY_NUM;
	}
	int num = num_stack[num_stack_index];
	num_stack[num_stack_index] = 0;
	num_stack_index--;
	return num;
}

void num_stack_clear(void) {
	memset(num_stack, 0, sizeof(num_stack));
	num_stack_index = -1;
}


/*操作符栈*/
char op_stack[100] = "\0";
int op_stack_index = -1;

bool isempty_op_stack() {
	return op_stack_index == -1;
}

void op_stack_push(char ch) {
	op_stack_index++;
	op_stack[op_stack_index] = ch;
}

char op_stack_pop(void) {
	if (isempty_op_stack()) {
		return EMPTY_CH;
	}
	char ch = op_stack[op_stack_index];
	op_stack[op_stack_index] = '\0';
	op_stack_index--;
	return ch;
}

char op_stack_top(void) {
	if (isempty_op_stack()) {
		return EMPTY_CH;
	}
	return op_stack[op_stack_index];
}

void op_stack_clear(void) {
	memset(op_stack, '\0', sizeof(op_stack));
	op_stack_index = -1;
}


/*优先级 */
int isp(char ch)
//栈内优先级
{
	switch (ch)
	{
		case '#':return 0;
		case '(':return 1;
		case '*':case '/':return 5;
		case '+':case '-':return 3;
		case ')':return 6;
	}
}
int icp(char ch)
//栈外优先级
{
	switch (ch)
	{
		case '#':return 0;
		case '(':return 6;
		case '*':case '/':return 4;
		case '+':case '-':return 2;
		case ')':return 1;
	}
}


/*运算符判断 */
bool isOperator(char c)
{
	switch (c)
	{
		case '+':
		case '-':
		case '*':
		case '/':
		case '(':
		case ')':
		case '#':
			return True;
			break;
		default:
			return False;
			break;
	}
}

/*单个数字字符判断 */
bool isDigit(char ch)
{
	return (ch >= '0' && ch <= '9');
}

/*数字判断 */
bool isNum(char* exp)
//数字包括 正数 负数 浮点数 (其中+3要进行特殊处理)
{
	char ch = exp[0];
	if (ch == '+' && strlen(exp) > 1)
		// 如 +3 就是储存 3
	{
		exp = exp + 1;	//把+删除
		ch = exp[0];		//更新一下ch
	}

	if (isDigit(ch) || (ch == '-' && strlen(exp) > 1))
		//储存各种数字, 包括正数,负数,浮点数
	{
		return True;
	}

	return False;
}

/*在表达式最后添加 # 标识符*/
void addTail(char* exp) {
	int i;
	for (i = 0; i < strlen(exp); ++i);
	exp[i] = ' ';
	exp[i + 1] = '#';
}

/*封装表达式中的项 */
struct Data {
	int data;	//数据本身
	int flag;	//0->char, 1->int
};
int _current = 0;

/*获取表达式中的下一项*/
struct Data NextContent(char* exp)
{
	char _next[100] = "\0";
	char ch;
	int index = 0;

	for (int i = _current; i < strlen(exp); ++i)
	{
		ch = exp[i];
		if (ch != ' ')
		{
			_next[index] = ch;
			index++;	//因为不同对象以空格隔开,所以只要不是空格就加到_next
		}
		else
		{
			while (exp[i] == ' ') {
				i++;
			}
			_current = i;	//_current指向下一个位置,结束当前对象的寻找
			break;
		}
	}

	if (isOperator(_next[0])) {
		struct Data d;
		d.data = _next[0];
		d.flag = 0;
		return d;
	}
	else {
		struct Data d;
		atoi(_next, &d.data);
		d.flag = 1;
		return d;
	}
}


/*根据运算符和两个操作数计算值 */
int Cal(int left, char op, int right)
{
	switch (op)
	{
		case '+':
			return left + right;
			break;
		case '-':
			return left - right;
			break;
		case '*':
			return left * right;
			break;
		case '/':
			return left / right;
			break;
		default:
			return left + right;
			break;
	}
}

/*输出后缀表达式 */
void showBackMode(struct Data result[], int size) {
	printf("The reverse polish notation is: ");
	for (int i = 0; i < size; ++i) {
		if (result[i].flag == 0) {
			printf("%c ", result[i].data);
		}
		else {
			printf("%d ", result[i].data);
		}
	}
	printf("\n");
}

/*计算后缀表达式的结果 */
int calResult(struct Data result[], int size)
{
	num_stack_clear();
	for (int i = 0; i < size; ++i) {
		if (result[i].flag == 1) {
			num_stack_push(result[i].data);
		}
		else {
			int right = num_stack_pop();
			int left = num_stack_pop();
			num_stack_push(Cal(left, result[i].data, right));
		}
	}
	return num_stack_pop();
}


/*顶层计算函数 */
int calculate(char* origin_exp, bool if_showrev) {
	char exp[100] = "\0";
	int pos = 0;
	for (int i = 0; i < strlen(origin_exp); ++i) {
		if (isOperator(origin_exp[i])) {
			exp[pos] = ' ';
			++pos;
			exp[pos] = origin_exp[i];
			++pos;
			exp[pos] = ' ';
			++pos;
		}
		else if (isDigit(origin_exp[i])) {
			exp[pos] = origin_exp[i];
			++pos;
		}
	}


	/*初始两个栈*/
	num_stack_clear();
	op_stack_clear();
	_current = 0;

	struct Data result[100];
	int index = 0;

	/*在表达式尾部添加结束标识符*/
	addTail(exp);

	op_stack_push('#');
	struct Data elem = NextContent(exp);
	while (!isempty_op_stack()) {
		char ch = elem.data;

		if (elem.flag == 1) {		//如果是操作数, 直接读入下一个内容
			result[index] = elem;
			index++;
			elem = NextContent(exp);
		}
		else if (elem.flag == 0) {	//如果是操作符,判断ch的优先级icp和当前栈顶操作符的优先级isp
			char topch = op_stack_top();
			if (isp(topch) < icp(ch)) {		//当前操作符优先级大,将ch压栈,读入下一个内容
				op_stack_push(ch);
				elem = NextContent(exp);
			}
			else if (isp(topch) > icp(ch)) {	//当前优先级小,推展并输出到结果中
				struct Data buf;
				buf.data = op_stack_pop();
				buf.flag = 0;
				result[index] = buf;
				index++;
			}
			else {
				if (op_stack_top() == '(') {	//如果退出的是左括号则读入下一个内容
					elem = NextContent(exp);
				}
				op_stack_pop();
			}
		}
	}

	if (if_showrev) {
		showBackMode(result, index);
	}

	return calResult(result, index);
}

/*判断表达式括号是否匹配*/
bool check_exp_bucket(char* exp) {
	char ch = '\0';

	for (int i = 0; i < strlen(exp); ++i) {
		if (exp[i] == '(') {
			bucket_stack_push('(');
		}
		else if (exp[i] == ')') {
			ch = bucket_stack_pop();
			if (ch == EMPTY_CH || ch != '(') {
				printf("Buckets in the exprssion you input do not match.\n");
				return False;
			}
		}
	}
	return isempty_bucket_stack();
}

/*判断表达式是否有非法符号*/
bool check_exp_notion(char* exp) {
	for (int i = 0; i < strlen(exp); ++i) {
		if (isDigit(exp[i]) || isOperator(exp[i]) || exp[i] == ' ') {
			continue;
		}
		else {
			printf("The operator we support: [+-*/()], you have input %c.\n", exp[i]);
			return False;
		}
	}
	return True;
}
/*计算器模块功能提示*/
void calc_usage()
{
	printf("[OPTION LIST]  \n");
	printf("--rev <exp> -> output the reverse polish notation \n");
	printf("<no option> -> just calculate the value  \n");
	printf("[EXAMPLE]    \n");
	printf("calc --rev 3 * ( 7 - 4 ) + 8 / 4 \n");
	printf("calc 1+2-(3*4)/5 \n");
}
/* 计算器主函数 */
void calcMain(char* expression)
{
	if (!strcmp(expression, "NULL")) {
		printf("Sorry, you should add a calc expressioin.\n");
		calc_usage();
	}
	else {
		char str_list[1][10] = { "--rev" };
		int flag[1] = { 1 };
		for (int i = 0; i < 1; ++i) {
			int j = 0;
			while (expression[j] != ' ' && expression[j] != '\0') {
				if (expression[j] != str_list[i][j]) {
					flag[i] = 0;
					break;
				}
				++j;
			}
		}


		if (flag[0]) {
			if (strlen(expression) > 5) {
				char* value = expression + 5;

				if (!check_exp_notion(value)) {
					printf("Please check the expression and try again.\n");
					printf("\n");
					return;
				}

				bucket_stack_clear();
				if (!check_exp_bucket(value)) {
					printf("Please check the expression and try again.\n");
					printf("\n");
					return;
				}


				int result = calculate(value, True);
				if (result != EMPTY_NUM) {
					printf("The result is %d\n", result);
				}
				else {
					calc_usage();
				}
			}
			else {
				printf("You should add the expression you want to calculate.\n");
				calc_usage();
			}

		}

		else {
			if (strlen(expression) > 0) {
				char* value = expression;

				if (!check_exp_notion(value)) {
					printf("Please check the expression and try again.\n");
					printf("\n");
					return;
				}

				bucket_stack_clear();
				if (!check_exp_bucket(value)) {
					printf("Please check the expression and try again.\n");
					printf("\n");
					return;
				}

				int result = calculate(value, False);
				if (result != EMPTY_NUM) {
					printf("The result is %d\n", result);
				}
				else {
					calc_usage();
				}
			}
			else {
				printf("You should add at least one expression.\n");
				calc_usage();
			}
		}
	}

	printf("\n");
}
