#include <iostream>  // 输入输出流库
#include <vector>    // 向量容器库
#include <string>    // 字符串库
#include <thread>    // 多线程支持库
#include <mutex>     // 互斥锁支持库
#include <condition_variable>  // 条件变量支持库
#include <queue>     // 队列容器库
#include <map>       // 映射容器库
#include <ctime>     // 时间库
#include <iomanip>   // 输入输出格式化库
#include <list>      // 链表容器库
#include <unordered_map>  // 无序映射容器库
#include <sstream>   // 字符串流库
#include <chrono>    // 时间库
#include <future>    // 异步任务支持库
#include <graphics.h> // EGE 图形库
#include <atomic>    // 原子操作支持库
#include <algorithm> // 算法库
#include <semaphore.h> // 信号量支持库
#include <memory>    // 智能指针支持库
#include <windows.h> // Windows API 支持库

#define MAX_PRIORITY_FS 5 // 定义最大优先级为5
const int BLOCK_SIZE = 64; // 定义块大小为64字节
const int BLOCK_COUNT = 1024; // 定义块数量为1024
const int METADATA_BLOCKS = 10; // 定义元数据块数量为10
const int BUFFER_PAGE_COUNT = 8; // 定义缓冲页数量为8

// 全局互斥锁，用于保护控制台输出
std::mutex g_consoleMutex;

// 线程安全的输出函数
void safePrint(const std::string& message) {
	std::lock_guard<std::mutex> lock(g_consoleMutex); // 加锁
	std::cout << message; // 输出消息
}

// 信号量用于进程同步
sem_t semaphore;

// 异步文件I/O类
class AsyncFileIO {
private:
	HANDLE fileHandle; // 文件句柄
	OVERLAPPED overlapped; // 重叠I/O结构
	std::atomic<bool> isOperationPending{false}; // 原子布尔值，表示是否有操作正在进行
	
public:
	AsyncFileIO() : fileHandle(INVALID_HANDLE_VALUE) { // 构造函数，初始化文件句柄为无效值
		ZeroMemory(&overlapped, sizeof(OVERLAPPED)); // 初始化重叠I/O结构
	}
	
	~AsyncFileIO() { // 析构函数
		if (fileHandle != INVALID_HANDLE_VALUE) { // 如果文件句柄有效
			CloseHandle(fileHandle); // 关闭文件句柄
		}
	}
	
	// 打开文件
	bool open(const std::string& fileName, DWORD accessMode, DWORD creationDisposition) {
		fileHandle = CreateFileA( // 创建文件
			fileName.c_str(), // 文件名
			accessMode, // 访问模式
			FILE_SHARE_READ | FILE_SHARE_WRITE, // 共享模式
			nullptr, // 安全属性
			creationDisposition, // 创建方式
			FILE_FLAG_OVERLAPPED, // 重叠I/O标志
			nullptr // 模板文件句柄
			);
		return fileHandle != INVALID_HANDLE_VALUE; // 返回文件句柄是否有效
	}
	
	// 异步读取文件
	bool readAsync(char* buffer, DWORD bytesToRead, DWORD& bytesRead) {
		if (isOperationPending) { // 如果已有操作正在进行
			return false; // 返回失败
		}
		
		ZeroMemory(&overlapped, sizeof(OVERLAPPED)); // 初始化重叠I/O结构
		isOperationPending = true; // 设置操作正在进行
		
		BOOL result = ReadFile( // 读取文件
			fileHandle, // 文件句柄
			buffer, // 缓冲区
			bytesToRead, // 要读取的字节数
			&bytesRead, // 实际读取的字节数
			&overlapped // 重叠I/O结构
			);
		
		if (!result && GetLastError() != ERROR_IO_PENDING) { // 如果读取失败且错误不是I/O挂起
			isOperationPending = false; // 设置操作未进行
			return false; // 返回失败
		}
		
		return true; // 返回成功
	}
	
	// 异步写入文件
	bool writeAsync(const char* buffer, DWORD bytesToWrite, DWORD& bytesWritten) {
		if (isOperationPending) { // 如果已有操作正在进行
			return false; // 返回失败
		}
		
		ZeroMemory(&overlapped, sizeof(OVERLAPPED)); // 初始化重叠I/O结构
		isOperationPending = true; // 设置操作正在进行
		
		BOOL result = WriteFile( // 写入文件
			fileHandle, // 文件句柄
			buffer, // 缓冲区
			bytesToWrite, // 要写入的字节数
			&bytesWritten, // 实际写入的字节数
			&overlapped // 重叠I/O结构
			);
		
		if (!result && GetLastError() != ERROR_IO_PENDING) { // 如果写入失败且错误不是I/O挂起
			isOperationPending = false; // 设置操作未进行
			return false; // 返回失败
		}
		
		return true; // 返回成功
	}
	
	// 检查操作是否完成
	bool isOperationComplete(DWORD& bytesTransferred) {
		if (!isOperationPending) { // 如果没有操作正在进行
			return false; // 返回失败
		}
		
		BOOL result = GetOverlappedResult( // 获取重叠I/O结果
			fileHandle, // 文件句柄
			&overlapped, // 重叠I/O结构
			&bytesTransferred, // 传输的字节数
			FALSE // 是否等待操作完成
			);
		
		if (result) { // 如果操作成功
			isOperationPending = false; // 设置操作未进行
			return true; // 返回成功
		}
		
		if (GetLastError() == ERROR_IO_INCOMPLETE) { // 如果操作未完成
			return false; // 返回失败
		}
		
		isOperationPending = false; // 设置操作未进行
		return false; // 返回失败
	}
	
	// 等待操作完成
	void waitForCompletion() {
		if (isOperationPending) { // 如果有操作正在进行
			DWORD bytesTransferred; // 传输的字节数
			GetOverlappedResult(fileHandle, &overlapped, &bytesTransferred, TRUE); // 获取重叠I/O结果并等待
			isOperationPending = false; // 设置操作未进行
		}
	}
};

// 进程类
class Process {
public:
	int id; // 进程ID
	int priority; // 进程优先级
	std::string name; // 进程名称
	std::string status; // 进程状态
	std::function<void()> task; // 进程任务
	std::string processIdentifier; // 进程标识符
	std::chrono::system_clock::time_point timestamp; // 时间戳
	std::future<void> taskFuture; // 异步任务的结果
	
	// 构造函数
	Process(int id, int priority, const std::string& name, std::function<void()> task, const std::string& processIdentifier)
	: id(id), priority(priority), name(name), status("Waiting"), task(task), processIdentifier(processIdentifier),
	timestamp(std::chrono::system_clock::now()) {}
	
	// 禁用拷贝构造函数和拷贝赋值运算符
	Process(const Process&) = delete;
	Process& operator=(const Process&) = delete;
	
	// 定义移动构造函数
	Process(Process&& other) noexcept//将 other 的资源移动到当前对象
	: id(other.id), priority(other.priority), name(std::move(other.name)), status(std::move(other.status)),
	task(std::move(other.task)), processIdentifier(std::move(other.processIdentifier)),
	timestamp(other.timestamp), taskFuture(std::move(other.taskFuture)) {}
	
	// 定义移动赋值运算符
	Process& operator=(Process&& other) noexcept {//支持进程对象的移动赋值操作
		if (this != &other) {
			id = other.id;
			priority = other.priority;
			name = std::move(other.name);
			status = std::move(other.status);
			task = std::move(other.task);
			processIdentifier = std::move(other.processIdentifier);
			timestamp = other.timestamp;
			taskFuture = std::move(other.taskFuture);
		}
		return *this;
	}
	
	// 重载 < 运算符，用于优先级队列的排序。
	bool operator<(const Process& other) const {
		if (priority == other.priority) {
			return timestamp > other.timestamp;
		}
		return priority > other.priority;
	}
	
	// 执行任务
	void execute() {
		status = "Running"; // 设置状态为运行中
		taskFuture = std::async(std::launch::async, [this]() { // 异步执行任务
			task(); // 执行任务
			status = "Finished"; // 设置状态为完成
		});
	}
	
	// 检查任务是否完成
	bool isFinished() const {
		return taskFuture.valid() && taskFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	}
};

// 进程管理器类
class ProcessManager {
private:
	std::priority_queue<Process> processQueue; // 优先级队列
	std::mutex processMutex; // 进程队列互斥锁
	std::condition_variable processCV; // 条件变量
	std::atomic<bool> isRunning{true}; // 调度器运行标志
	std::queue<std::string> messageQueue; // 进程间的消息队列
	std::mutex messageMutex; // 消息队列互斥锁
	std::condition_variable messageCV; // 消息队列的条件变量
	sem_t semaphore; // 信号量
	
public:
	// 构造函数
	ProcessManager() {
		sem_init(&semaphore, 0, 1); // 初始化信号量，初始值为1
	}
	
	// 析构函数
	~ProcessManager() {
		sem_destroy(&semaphore); // 销毁信号量
	}
	
	// 添加进程
	void addProcess(Process&& process) {
		sem_wait(&semaphore); // 进入临界区
		std::lock_guard<std::mutex> lock(processMutex); // 加锁
		processQueue.emplace(std::move(process)); // 将进程加入队列
		processCV.notify_all(); // 通知所有等待的线程
		sem_post(&semaphore); // 离开临界区
	}
	
	// 获取下一个进程
	Process getNextProcess() {
		sem_wait(&semaphore); // 进入临界区
		std::unique_lock<std::mutex> lock(processMutex); // 加锁
		processCV.wait(lock, [this]() { return !processQueue.empty(); }); // 等待队列不为空
		Process process = std::move(const_cast<Process&>(processQueue.top())); // 获取队列顶部的进程
		processQueue.pop(); // 移除队列顶部的进程
		sem_post(&semaphore); // 离开临界区
		return process; // 返回进程
	}
	
	// 检查是否有进程在等待
	bool hasProcesses() {
		std::lock_guard<std::mutex> lock(processMutex); // 加锁
		return !processQueue.empty(); // 返回队列是否为空
	}
	
	// 启动调度器
	void startScheduler() {
		while (isRunning || hasProcesses()) { // 如果调度器在运行或队列不为空
			if (hasProcesses()) { // 如果有进程在等待
				Process process = getNextProcess(); // 获取下一个进程
				safePrint("[Scheduler] Running Process: " + process.name + " (Priority: " + std::to_string(process.priority) + ")\n"); // 输出进程信息
				process.execute(); // 执行任务
				
				// 检查任务是否完成
				while (!process.isFinished()) {
					std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 非阻塞等待
				}
				
				safePrint("[Scheduler] Finished Process: " + process.name + "\n"); // 输出任务完成信息
			}
		}
	}
	
	// 停止调度器
	void stopScheduler() {
		isRunning = false; // 设置调度器停止
		processCV.notify_all(); // 通知所有等待的线程
	}
	
	// 发送消息
	void sendMessage(const std::string& message) {
		std::lock_guard<std::mutex> lock(messageMutex); // 加锁
		messageQueue.push(message); // 将消息加入队列
		messageCV.notify_all(); // 通知所有等待的线程
	}
	
	// 接收消息
	std::string receiveMessage() {
		std::unique_lock<std::mutex> lock(messageMutex); // 加锁
		if (messageQueue.empty()) { // 如果消息队列为空
			return "[Message] 消息队列为空，没有可接收的消息。"; // 返回提示信息
		}
		std::string message = messageQueue.front(); // 获取队列顶部的消息
		messageQueue.pop(); // 移除队列顶部的消息
		return message; // 返回消息
	}
};

// FAT 表类
class FAT {
public:
	std::vector<int> table; // FAT 表，-1 表示结束
	FAT() : table(BLOCK_COUNT, -2) {} // 构造函数，初始化FAT表
};

// 目录项类
class DirectoryEntry {
public:
	std::string fileName; // 文件名
	time_t creationTime; // 创建时间
	int startBlock; // 起始块
	int fileSize; // 文件大小
	bool readOnly; // 文件权限：是否只读
	bool isOpen; // 文件是否正在被操作
	FAT fat; // FAT表
	std::mutex fileMutex; // 文件独立的互斥锁
	
	// 构造函数
	DirectoryEntry() : fileName(""), creationTime(0), startBlock(-1), fileSize(0), readOnly(false), isOpen(false) {}
	
	// 带参数的构造函数
	DirectoryEntry(const std::string& name, int start, int size, bool isReadOnly = false)
	: fileName(name), startBlock(start), fileSize(size), readOnly(isReadOnly), isOpen(false) {
		creationTime = time(nullptr); // 设置创建时间
	}
	
	// 关闭文件
	void closeFile() {
		std::lock_guard<std::mutex> lock(fileMutex); // 加锁
		isOpen = false; // 设置文件为关闭状态
		safePrint("File closed: " + fileName + "\n"); // 输出关闭文件信息
	}
	
	// 获取块数量
	int getBlockCount() const {
		int count = 0;
		int block = startBlock;
		while (block != -1) { // 遍历FAT表
			count++;
			block = fat.table[block];
		}
		return count; // 返回块数量
	}
	
	// 打印文件信息
	void printInfo(const std::vector<std::vector<char>>& disk) const {
		char timeBuffer[26];
		ctime_s(timeBuffer, sizeof(timeBuffer), &creationTime); // 格式化时间
		timeBuffer[24] = '\0';
		
		std::ostringstream oss;
		oss << std::left << std::setw(15) << fileName
		<< std::setw(25) << timeBuffer
		<< std::setw(10) << std::to_string(fileSize) + " bytes"
		<< "  " << std::setw(10) << (readOnly ? "ReadOnly" : "Writable")
		<< std::setw(10) << (isOpen ? "Open" : "Closed");
		
		oss << std::setw(20) << "Blocks: ";
		int block = startBlock;
		bool firstBlock = true;
		while (block != -1) { // 遍历FAT表
			if (!firstBlock) {
				oss << ", ";
			}
			oss << std::to_string(block);
			firstBlock = false;
			block = fat.table[block];
		}
		oss << "\n";
		
		safePrint(oss.str()); // 输出文件信息
	}
};

// 缓冲页类
class BufferPage {
public:
	int blockID; // 块ID
	std::string owner; // 所有者
	bool isModified; // 是否被修改
	std::vector<char> data; // 数据
	std::chrono::system_clock::time_point lastAccessTime; // 最后访问时间
	
	// 构造函数
	BufferPage() : blockID(-1), owner(""), isModified(false), data(BLOCK_SIZE, 0) {}
	
	// 加载数据
	void loadData(const std::vector<char>& diskBlockData) {
		data = diskBlockData; // 复制数据
		lastAccessTime = std::chrono::system_clock::now(); // 更新最后访问时间
	}
	
	// 写入数据
	void writeData(const std::string& content, size_t offset = 0) {
		size_t contentIndex = 0;
		for (size_t i = offset; i < data.size() && contentIndex < content.size(); ++i) {
			data[i] = content[contentIndex++]; // 写入数据
		}
		isModified = true; // 设置修改标志
		lastAccessTime = std::chrono::system_clock::now(); // 更新最后访问时间
	}
};

// 缓冲管理器类
class BufferManager {
private:
	std::unordered_map<int, std::list<BufferPage>::iterator> pageTable; // 页表（哈希表）
	std::list<BufferPage> bufferPool; // 缓冲池
	size_t maxPages; // 最大页数
	std::mutex bufferMutex; // 互斥锁
	
public:
	// 构造函数
	BufferManager(size_t pageCount) : maxPages(pageCount) {}
	
	// 获取缓冲页
	BufferPage* getPage(int blockID, const std::vector<char>& diskBlockData, const std::string& owner) {
		std::lock_guard<std::mutex> lock(bufferMutex); // 加锁
		// 模拟 I/O 延迟
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		// 如果缓冲页已存在，移动到缓冲池头部并返回
		if (pageTable.find(blockID) != pageTable.end()) {//查找成功
			auto pageIt = pageTable[blockID];
			bufferPool.splice(bufferPool.begin(), bufferPool, pageIt); // 移动到头部
			pageIt->lastAccessTime = std::chrono::system_clock::now(); // 更新访问时间
			return &(*pageIt);
		}
		
		// 如果缓冲池已满，找到最近最少使用的缓冲页进行置换
		if (bufferPool.size() >= maxPages) {
			auto lruPageIt = bufferPool.end();
			auto oldestTime = std::chrono::system_clock::now();
			
			// 遍历缓冲池，找到最近最少使用的缓冲页
			for (auto it = bufferPool.begin(); it != bufferPool.end(); ++it) {
				if (it->lastAccessTime < oldestTime) {
					oldestTime = it->lastAccessTime;
					lruPageIt = it;
				}
			}
			
			// 如果找到 LRU 缓冲页，将其写回磁盘并移除
			if (lruPageIt != bufferPool.end()) {
				if (lruPageIt->isModified) {
					safePrint("[Buffer] Writing back modified page: Block " + std::to_string(lruPageIt->blockID) + "\n");
					writeBack(*lruPageIt, const_cast<std::vector<char>&>(diskBlockData));
				}
				safePrint("[Buffer] Evicting LRU page: Block " + std::to_string(lruPageIt->blockID) + "\n");
				pageTable.erase(lruPageIt->blockID);
				bufferPool.erase(lruPageIt);
			}
		}
		
		// 如果未满，创建新的缓冲页并添加到缓冲池头部
		BufferPage newPage;
		newPage.blockID = blockID;
		newPage.owner = owner;
		newPage.loadData(diskBlockData);
		newPage.lastAccessTime = std::chrono::system_clock::now(); // 设置访问时间
		bufferPool.push_front(newPage);
		pageTable[blockID] = bufferPool.begin();
		
		return &(*bufferPool.begin());
	}
	
	// 写回数据
	void writeBack(BufferPage& page, std::vector<char>& diskBlockData) {
		std::lock_guard<std::mutex> lock(bufferMutex); // 加锁
		// 模拟 I/O 延迟
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		if (!page.isModified) return; // 如果未修改，直接返回
		
		for (size_t i = 0; i < page.data.size(); ++i) {
			diskBlockData[i] = page.data[i]; // 写回数据
		}
		page.isModified = false; // 设置未修改标志
	}
	
	// 打印缓冲状态
	void printBufferStatus() {
		std::lock_guard<std::mutex> lock(bufferMutex); // 加锁
		safePrint("Current Buffer Pages: \n");
		for (const auto& page : bufferPool) {
			safePrint("Block ID: " + std::to_string(page.blockID)
				+ ", Owner: " + page.owner
				+ ", Modified: " + (page.isModified ? "Yes" : "No")
				+ ", Last Access Time: " + std::to_string(std::chrono::system_clock::to_time_t(page.lastAccessTime)) + "\n");
		}
		safePrint("--------------------------------\n");
	}
	
	// 获取缓冲池
	const std::list<BufferPage>& getBufferPool() const {
		return bufferPool;
	}
};

// 文件系统类
class FileSystem {
private:
	std::vector<bool> bitMap; // 位图
	FAT fat; // FAT表
	std::map<std::string, std::unique_ptr<DirectoryEntry>> directory; // 目录
	std::vector<std::vector<char>> disk; // 磁盘
	mutable std::mutex fileMutex; // 文件互斥锁
	BufferManager bufferManager; // 缓冲管理器
	bool isVisualizationRunning; // 可视化是否在运行
	ProcessManager processManager; // 进程管理器
	std::map<std::string, std::unique_ptr<AsyncFileIO>> openFiles; // 打开的文件句柄
	sem_t fileSemaphore; // 文件操作信号量
	
public:
	// 关闭所有打开的文件
	void closeAllFiles() {
		std::lock_guard<std::mutex> lock(fileMutex); // 加锁
		for (auto& entry : directory) {
			if (entry.second->isOpen) {
				entry.second->closeFile(); // 关闭文件
			}
		}
	}
	
	// 交互菜单函数
	void interactiveMenu();
	
	// 构造函数
	FileSystem() : bitMap(BLOCK_COUNT, false), disk(BLOCK_COUNT, std::vector<char>(BLOCK_SIZE, 0)), bufferManager(BUFFER_PAGE_COUNT), isVisualizationRunning(false) {
		initializeDisk(); // 初始化磁盘
		sem_init(&fileSemaphore, 0, 1); // 初始化信号量
	}
	
	// 析构函数
	~FileSystem() {
		sem_destroy(&fileSemaphore); // 销毁信号量
	}
	
	// 获取空闲块数量
	int getFreeBlockCount() const {
		int count = 0;
		for (bool used : bitMap) { // 遍历位图
			if (!used) ++count; // 统计空闲块
		}
		return count; // 返回空闲块数量
	}
	
	// 获取空闲块列表
	std::vector<int> getFreeBlocks() const {
		std::vector<int> freeBlocks;
		for (int i = 0; i < BLOCK_COUNT; ++i) { // 遍历位图
			if (!bitMap[i]) {
				freeBlocks.push_back(i); // 添加空闲块
			}
		}
		return freeBlocks; // 返回空闲块列表
	}
	
	// 获取文件的盘块索引列表
	std::vector<int> getFileBlocks(std::string& name) {
		std::vector<int> blocks;
		if (directory.find(name) == directory.end()) { // 如果文件不存在
			safePrint("Error: File not found!\n"); // 输出错误信息
			return blocks; // 返回空列表
		}
		
		int block = directory[name]->startBlock; // 获取起始块
		while (block != -1) { // 遍历FAT表
			blocks.push_back(block);
			block = fat.table[block];
		}
		
		return blocks; // 返回块列表
	}
	
	// 打印FAT表
	void printFAT() const {
		safePrint("FAT Table (First 50 entries):\n");
		for (int i = 0; i < 50; ++i) { // 打印前50个FAT表项
			safePrint("Block " + std::to_string(i) + " -> " + std::to_string(fat.table[i]) + "\n");
		}
	}
	
	// 初始化磁盘
	void initializeDisk() {
		for (int i = 0; i < METADATA_BLOCKS; ++i) { // 初始化元数据块
			bitMap[i] = true; // 设置块为已使用
			fat.table[i] = -1; // 设置FAT表项为结束
		}
	}
	
	// 异步创建文件
	std::future<void> createFileAsync(const std::string& name, const std::string& content, bool isReadOnly = false, const std::string& processIdentifier = "") {
		return std::async(std::launch::async, [this, name, content, isReadOnly, processIdentifier]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 模拟I/O延迟
			this->createFile(name, content, isReadOnly, processIdentifier); // 创建文件
		});
	}
	
	// 创建文件
	void createFile(const std::string& name, const std::string& content, bool isReadOnly = false, const std::string& processIdentifier = "") {
		sem_wait(&fileSemaphore); // 进入临界区
		std::lock_guard<std::mutex> lock(fileMutex); // 加锁
		
		if (directory.find(name) != directory.end()) { // 如果文件已存在
			safePrint("Error: File already exists!\n"); // 输出错误信息
			return;
		}
		
		int blocksNeeded = (content.size() + BLOCK_SIZE - 1) / BLOCK_SIZE; // 计算需要的块数
		std::vector<int> allocatedBlocks; // 分配的块列表
		
		for (int i = 0; i < BLOCK_COUNT && blocksNeeded > 0; ++i) { // 遍历位图
			if (!bitMap[i]) { // 如果块空闲
				bitMap[i] = true; // 设置块为已使用
				allocatedBlocks.push_back(i); // 添加到分配的块列表
				--blocksNeeded; // 减少需要的块数
			}
		}
		
		if (blocksNeeded > 0) { // 如果没有足够的空闲块
			safePrint("Error: Not enough disk space!\n"); // 输出错误信息
			return;
		}
		
		for (size_t i = 0; i < allocatedBlocks.size() - 1; ++i) { // 设置FAT表项
			fat.table[allocatedBlocks[i]] = allocatedBlocks[i + 1];
		}
		fat.table[allocatedBlocks.back()] = -1; // 设置最后一个块为结束
		
		size_t contentIndex = 0;
		for (int block : allocatedBlocks) { // 写入数据
			for (int j = 0; j < BLOCK_SIZE && contentIndex < content.size(); ++j) {
				disk[block][j] = content[contentIndex++];
			}
		}
		
		directory[name] = std::make_unique<DirectoryEntry>(name, allocatedBlocks[0], content.size(), isReadOnly); // 创建目录项
		safePrint("File created: " + name + "\n"); // 输出创建文件信息
		
		// 发送消息
		std::string message = processIdentifier + " 完成了文件创建操作：" + name;
		processManager.sendMessage(message); // 发送消息
		safePrint("[Message] 消息已发送: " + message + "\n");
		sem_post(&fileSemaphore); // 离开临界区
	}
	
	// 异步删除文件
	std::future<void> deleteFileAsync(const std::string& name, const std::string& processIdentifier) {
		return std::async(std::launch::async, [this, name, processIdentifier]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 模拟I/O延迟
			this->deleteFile(name, processIdentifier); // 删除文件
		});
	}
	
	// 删除文件
	void deleteFile(const std::string& name, const std::string& processIdentifier) {
		std::lock_guard<std::mutex> lock(fileMutex); // 加锁
		
		if (directory.find(name) == directory.end()) { // 如果文件不存在
			safePrint("Error: File not found!\n"); // 输出错误信息
			return;
		}
		
		// 检查文件是否打开
		if (directory[name]->isOpen) { // 如果文件已打开
			safePrint("[File Protection] Error: File is currently open and cannot be deleted: " + name + "\n"); // 输出错误信息
			return;
		}
		
		int block = directory[name]->startBlock; // 获取起始块
		while (block != -1) { // 遍历FAT表
			int nextBlock = fat.table[block];
			fat.table[block] = 0; // 重置FAT表项
			bitMap[block] = false; // 设置块为未使用
			block = nextBlock;
		}
		
		directory.erase(name); // 删除目录项
		safePrint("File deleted: " + name + "\n"); // 输出删除文件信息
		
		// 发送消息
		std::string message = processIdentifier + " 完成了文件删除操作：" + name;
		processManager.sendMessage(message); // 发送消息
		safePrint("[Message] 消息已发送: " + message + "\n");
	}
	
	// 异步查看文件内容
	std::future<void> viewFileAsync(const std::string& name, const std::string& processIdentifier) {
		return std::async(std::launch::async, [this, name, processIdentifier]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 模拟I/O延迟
			this->viewFile(name, processIdentifier); // 查看文件内容
		});
	}
	
	// 查看文件内容
	void viewFile(const std::string& name, const std::string& processIdentifier) {
		std::lock_guard<std::mutex> lock(fileMutex); // 加锁
		
		if (directory.find(name) == directory.end()) { // 如果文件不存在
			safePrint("Error: File not found!\n"); // 输出错误信息
			return;
		}
		
		// 自动打开文件
		if (!directory[name]->isOpen) { // 如果文件未打开
			directory[name]->isOpen = true; // 设置文件为打开状态
			safePrint("File opened: " + name + "\n"); // 输出打开文件信息
		}
		
		int block = directory[name]->startBlock; // 获取起始块
		while (block != -1) { // 遍历FAT表
			BufferPage* page = bufferManager.getPage(block, disk[block], "viewer"); // 获取缓冲页
			for (int i = 0; i < BLOCK_SIZE && page->data[i] != 0; ++i) { // 输出文件内容
				safePrint(std::string(1, page->data[i]));
			}
			block = fat.table[block];
		}
		safePrint("\n");
		
		// 发送消息
		std::string message = processIdentifier + " 完成了文件查看操作：" + name;
		processManager.sendMessage(message); // 发送消息
		safePrint("[Message] 消息已发送: " + message + "\n");
	}
	
	// 异步查看文件的指定盘块内容
	std::future<void> viewFileBlockAsync(const std::string& name, int blockIndex, const std::string& processIdentifier) {
		return std::async(std::launch::async, [this, name, blockIndex, processIdentifier]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 模拟I/O延迟
			this->viewFileBlock(name, blockIndex, processIdentifier); // 查看文件的指定盘块内容
		});
	}
	
	// 查看文件的指定盘块内容
	void viewFileBlock(const std::string& name, int blockIndex, const std::string& processIdentifier) {
		std::lock_guard<std::mutex> lock(fileMutex); // 加锁
		
		if (directory.find(name) == directory.end()) { // 如果文件不存在
			safePrint("Error: File not found!\n"); // 输出错误信息
			return;
		}
		
		// 自动打开文件
		if (!directory[name]->isOpen) { // 如果文件未打开
			directory[name]->isOpen = true; // 设置文件为打开状态
			safePrint("File opened: " + name + "\n"); // 输出打开文件信息
		}
		
		int block = directory[name]->startBlock; // 获取起始块
		int currentBlockIndex = 0;
		
		while (block != -1) { // 遍历FAT表
			if (currentBlockIndex == blockIndex) { // 如果找到指定块
				BufferPage* page = bufferManager.getPage(block, disk[block], "viewer"); // 获取缓冲页
				safePrint("Block " + std::to_string(blockIndex) + " (Disk Block " + std::to_string(block) + ") content: ");
				for (int i = 0; i < BLOCK_SIZE && page->data[i] != 0; ++i) { // 输出块内容
					safePrint(std::string(1, page->data[i]));
				}
				safePrint("\n");
				// 发送消息
				std::string message = processIdentifier + " 完成了文件盘块查看操作：" + name + " (Block " + std::to_string(blockIndex) + ")";
				processManager.sendMessage(message); // 发送消息
				safePrint("[Message] 消息已发送: " + message + "\n");
				return; // 找到指定块后立即返回
			}
			block = fat.table[block];
			currentBlockIndex++;
		}
		safePrint("Error: Block index out of range! File has only " + std::to_string(currentBlockIndex) + " blocks.\n"); // 输出错误信息
	}
	
	// 异步修改文件内容
	std::future<void> modifyFileAsync(const std::string& name, const std::string& content, const std::string& processIdentifier) {
		return std::async(std::launch::async, [this, name, content, processIdentifier]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 模拟I/O延迟
			this->modifyFile(name, content, processIdentifier); // 修改文件内容
		});
	}
	
	// 修改文件内容
	void modifyFile(const std::string& name, const std::string& content, const std::string& processIdentifier) {
		std::lock_guard<std::mutex> lock(fileMutex);
		
		if (directory.find(name) == directory.end()) {
			safePrint("Error: File not found!\n");
			return;
		}
		
		// 自动打开文件
		if (!directory[name]->isOpen) {
			directory[name]->isOpen = true;
			safePrint("File opened: " + name + "\n");
		}
		
		if (directory[name]->readOnly) {
			safePrint("[Permission] Error: File is read-only: " + name + "\n");
			return;
		}
		safePrint("[Permission] File is writable: " + name + "\n");
		
		// 清空文件的所有块
		int block = directory[name]->startBlock;
		while (block != -1) {
			BufferPage* page = bufferManager.getPage(block, disk[block], "modifier");
			std::fill(page->data.begin(), page->data.end(), '\0'); // 清空块内容
			page->isModified = true;
			block = fat.table[block];
		}
		
		// 写入新内容
		block = directory[name]->startBlock;
		size_t contentIndex = 0;
		
		while (block != -1 && contentIndex < content.size()) {
			BufferPage* page = bufferManager.getPage(block, disk[block], "modifier");
			for (int j = 0; j < BLOCK_SIZE && contentIndex < content.size(); ++j) {
				page->data[j] = content[contentIndex++];
			}
			page->isModified = true;
			block = fat.table[block];
		}
		
		directory[name]->fileSize = content.size();
		safePrint("File modified: " + name + "\n");
		
		// 发送消息
		std::string message = processIdentifier + " 完成了文件内容修改操作：" + name;
		processManager.sendMessage(message);
		safePrint("[Message] 消息已发送: " + message + "\n");
	}
	// 异步修改文件的指定盘块内容
	std::future<void> modifyFileBlockAsync(const std::string& name, int blockIndex, const std::string& content, const std::string& processIdentifier) {
		return std::async(std::launch::async, [this, name, blockIndex, content, processIdentifier]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 模拟I/O延迟
			this->modifyFileBlock(name, blockIndex, content, processIdentifier); // 修改文件的指定盘块内容
		});
	}
	
	// 修改文件的指定盘块内容
	void modifyFileBlock(const std::string& name, int blockIndex, const std::string& content, const std::string& processIdentifier) {
		std::lock_guard<std::mutex> lock(fileMutex); // 加锁
		
		if (directory.find(name) == directory.end()) { // 如果文件不存在
			safePrint("Error: File not found!\n"); // 输出错误信息
			return;
		}
		
		// 自动打开文件
		if (!directory[name]->isOpen) { // 如果文件未打开
			directory[name]->isOpen = true; // 设置文件为打开状态
			safePrint("File opened: " + name + "\n"); // 输出打开文件信息
		}
		
		if (directory[name]->readOnly) { // 如果文件只读
			safePrint("[Permission] Error: File is read-only: " + name + "\n"); // 输出错误信息
			return;
		}
		safePrint("[Permission] File is writable: " + name + "\n"); // 输出文件可写信息
		
		int block = directory[name]->startBlock; // 获取起始块
		int currentBlockIndex = 0;
		
		while (block != -1) { // 遍历FAT表
			if (currentBlockIndex == blockIndex) { // 如果找到指定块
				BufferPage* page = bufferManager.getPage(block, disk[block], "modifier"); // 获取缓冲页
				
				// 获取块大小（假设块大小是固定的，或者通过其他方式获取）
				const int blockSize = 4096; // 假设块大小为 4096 字节
				// 如果块大小是动态的，可以通过 page 或其他方式获取
				
				// 清空块内容
				page->writeData(std::string(blockSize, '\0')); // 用空字符填充整个块
				
				// 写入新内容
				page->writeData(content); // 写入新内容
				
				safePrint("Block " + std::to_string(blockIndex) + " (Disk Block " + std::to_string(block) + ") modified.\n"); // 输出修改块信息
				
				// 发送消息
				std::string message = processIdentifier + " 完成了文件盘块内容修改操作：" + name + " (Block " + std::to_string(blockIndex) + ")";
				processManager.sendMessage(message); // 发送消息
				safePrint("[Message] 消息已发送: " + message + "\n");
				return;
			}
			block = fat.table[block];
			currentBlockIndex++;
		}
		
		safePrint("Error: Block index out of range! File has only " + std::to_string(currentBlockIndex) + " blocks.\n"); // 输出错误信息
	}
	
	// 查看FAT表
	void viewFAT() const {
		std::lock_guard<std::mutex> lock(fileMutex); // 加锁
		safePrint("FAT Table:\n");
		for (int i = 0; i < BLOCK_COUNT; ++i) { // 遍历FAT表
			safePrint("Block " + std::to_string(i) + " -> " + std::to_string(fat.table[i]) + "\n");
		}
	}
	
	// 可视化函数
	void visualize() {
		initgraph(1200, 1000); // 初始化图形窗口
		setbkcolor(WHITE); // 设置背景颜色为白色
		cleardevice(); // 清屏
		
		while (isVisualizationRunning) { // 如果可视化在运行
			try {
				const std::list<BufferPage>& bufferPool = bufferManager.getBufferPool(); // 获取缓冲池
				const std::vector<bool>& bitMap = this->bitMap; // 获取位图
				
				cleardevice(); // 清屏
				
				setcolor(BLACK); // 设置颜色为黑色
				outtextxy(10, 10, "Buffer Pool:"); // 输出文本
				
				setfillcolor(WHITE); // 设置填充颜色为白色
				bar(10, 30, 1190, 200); // 绘制矩形
				
				int y = 30;
				for (const auto& page : bufferPool) { // 遍历缓冲池
					char buffer[100];
					sprintf(buffer, "Block %d: Owner=%s, Modified=%s, Last Access=%lld", // 格式化缓冲页信息
						page.blockID,
						page.owner.c_str(),
						page.isModified ? "Yes" : "No",
						std::chrono::system_clock::to_time_t(page.lastAccessTime));
					outtextxy(20, y, buffer); // 输出缓冲页信息
					y += 20;
				}
				
				setcolor(BLUE); // 设置颜色为蓝色
				outtextxy(10, 220, "Disk Blocks:"); // 输出文本
				for (int i = 0; i < BLOCK_COUNT; ++i) { // 遍历位图
					if (bitMap[i]) { // 如果块已使用
						setcolor(RED); // 设置颜色为红色
					} else {
						setcolor(GREEN); // 设置颜色为绿色
					}
					rectangle(20 + (i % 50) * 10, 240 + (i / 50) * 10, 30 + (i % 50) * 10, 250 + (i / 50) * 10); // 绘制矩形
				}
				
				setcolor(BLACK); // 设置颜色为黑色
				outtextxy(10, 500, "Directory:"); // 输出文本
				
				int tableX = 20;
				int tableY = 520;
				int colWidth[] = {150, 200, 100, 100, 150}; // 列宽
				
				setcolor(BLUE); // 设置颜色为蓝色
				rectangle(tableX, tableY, tableX + colWidth[0], tableY + 20); // 绘制矩形
				outtextxy(tableX + 5, tableY + 5, "File Name"); // 输出文本
				rectangle(tableX + colWidth[0], tableY, tableX + colWidth[0] + colWidth[1], tableY + 20); // 绘制矩形
				outtextxy(tableX + colWidth[0] + 5, tableY + 5, "Creation Time"); // 输出文本
				rectangle(tableX + colWidth[0] + colWidth[1], tableY, tableX + colWidth[0] + colWidth[1] + colWidth[2], tableY + 20); // 绘制矩形
				outtextxy(tableX + colWidth[0] + colWidth[1] + 5, tableY + 5, "Size"); // 输出文本
				rectangle(tableX + colWidth[0] + colWidth[1] + colWidth[2], tableY, tableX + colWidth[0] + colWidth[1] + colWidth[2] + colWidth[3], tableY + 20); // 绘制矩形
				outtextxy(tableX + colWidth[0] + colWidth[1] + colWidth[2] + 5, tableY + 5, "Permission"); // 输出文本
				rectangle(tableX + colWidth[0] + colWidth[1] + colWidth[2] + colWidth[3], tableY, tableX + colWidth[0] + colWidth[1] + colWidth[2] + colWidth[3] + colWidth[4], tableY + 20); // 绘制矩形
				outtextxy(tableX + colWidth[0] + colWidth[1] + colWidth[2] + colWidth[3] + 5, tableY + 5, "Blocks"); // 输出文本
				
				setcolor(BLACK); // 设置颜色为黑色
				int row = 1;
				for (const auto& entry : directory) { // 遍历目录
					int rowY = tableY + 20 * row;
					
					rectangle(tableX, rowY, tableX + colWidth[0], rowY + 20); // 绘制矩形
					outtextxy(tableX + 5, rowY + 5, entry.first.c_str()); // 输出文件名
					
					char timeBuffer[26];
					ctime_s(timeBuffer, sizeof(timeBuffer), &entry.second->creationTime); // 格式化时间
					timeBuffer[24] = '\0';
					rectangle(tableX + colWidth[0], rowY, tableX + colWidth[0] + colWidth[1], rowY + 20); // 绘制矩形
					outtextxy(tableX + colWidth[0] + 5, rowY + 5, timeBuffer); // 输出创建时间
					
					char sizeBuffer[20];
					sprintf(sizeBuffer, "%d bytes", entry.second->fileSize); // 格式化文件大小
					rectangle(tableX + colWidth[0] + colWidth[1], rowY, tableX + colWidth[0] + colWidth[1] + colWidth[2], rowY + 20); // 绘制矩形
					outtextxy(tableX + colWidth[0] + colWidth[1] + 5, rowY + 5, sizeBuffer); // 输出文件大小
					
					rectangle(tableX + colWidth[0] + colWidth[1] + colWidth[2], rowY, tableX + colWidth[0] + colWidth[1] + colWidth[2] + colWidth[3], rowY + 20); // 绘制矩形
					outtextxy(tableX + colWidth[0] + colWidth[1] + colWidth[2] + 5, rowY + 5, entry.second->readOnly ? "ReadOnly" : "Writable"); // 输出文件权限
					
					std::stringstream blocks;
					int block = entry.second->startBlock; // 获取起始块
					while (block != -1) { // 遍历FAT表
						blocks << block << " ";
						block = fat.table[block];
					}
					rectangle(tableX + colWidth[0] + colWidth[1] + colWidth[2] + colWidth[3], rowY, tableX + colWidth[0] + colWidth[1] + colWidth[2] + colWidth[3] + colWidth[4], rowY + 20); // 绘制矩形
					outtextxy(tableX + colWidth[0] + colWidth[1] + colWidth[2] + colWidth[3] + 5, rowY + 5, blocks.str().c_str()); // 输出块列表
					
					row++;
				}
				
				delay(100); // 延迟100毫秒
			} catch (const std::exception& e) { // 捕获异常
				safePrint("Error in visualize: " + std::string(e.what()) + "\n"); // 输出错误信息
				break;
			}
		}
		
		closegraph(); // 关闭图形窗口
	}
	
	// 启动可视化
	void startVisualization() {
		if (!isVisualizationRunning) { // 如果可视化未运行
			safePrint("[Visualization] Starting visualization...\n"); // 输出启动可视化信息
			isVisualizationRunning = true; // 设置可视化运行标志
			std::thread visualizationThread(&FileSystem::visualize, this); // 启动可视化线程
			visualizationThread.detach(); // 分离线程
		}
	}
	
	// 停止可视化
	void stopVisualization() {
		safePrint("[Visualization] Stopping visualization...\n"); // 输出停止可视化信息
		isVisualizationRunning = false; // 设置可视化停止标志
	}
};

// 文件系统交互菜单函数
void FileSystem::interactiveMenu() {
	safePrint("[Menu] Interactive menu started.\n"); // 输出交互菜单启动信息
	startVisualization(); // 启动可视化
	
	// 启动调度器线程
	std::thread schedulerThread([this]() { processManager.startScheduler(); });
	
	while (true) {
		std::vector<std::string> operations; // 操作列表
		std::string input;
		
		safePrint("\n===== 文件系统菜单 =====\n");
		safePrint("1. 创建文件: 1 <文件名> <内容> <优先级>\n");
		safePrint("2. 删除文件: 2 <文件名> <优先级>\n");
		safePrint("3. 查看文件内容: 3 <文件名> <优先级>\n");
		safePrint("4. 修改文件内容: 4 <文件名> <新内容> <优先级>\n");
		safePrint("5. 查看文件的指定盘块内容: 5 <文件名> <盘块索引> <优先级>\n");
		safePrint("6. 修改文件的指定盘块内容: 6 <文件名> <盘块索引> <新内容> <优先级>\n");
		safePrint("7. 查看FAT表: 7 <优先级>\n");
		safePrint("8. 退出: 8\n");
		safePrint("========================\n");
		safePrint("请输入多个操作（每行一个操作，输入 'end' 结束输入）：\n");
		
		while (true) {
			std::getline(std::cin, input); // 获取用户输入
			if (input == "end") { // 如果输入为 'end'
				break;
			}
			operations.push_back(input); // 将操作加入列表
		}
		
		if (operations.empty()) { // 如果没有输入任何操作
			safePrint("没有输入任何操作。\n"); // 输出提示信息
			continue;
		}
		
		// 暂停调度器线程
		processManager.stopScheduler();
		
		for (const auto& op : operations) { // 遍历操作列表
			std::istringstream iss(op); // 将操作字符串转换为输入流
			int command;
			iss >> command; // 获取命令
			
			if (command == 1) { // 创建文件
				std::string fileName, content, processIdentifier;
				int priority = 5; // 默认优先级为5
				iss >> fileName >> content >> priority >> processIdentifier;
				if (priority > 5) { // 如果优先级大于5
					safePrint("错误：优先级输入错误，优先级必须小于或等于5。请重新输入。\n"); // 输出错误信息
					continue; // 跳过当前操作
				}
				processManager.addProcess(Process(1, priority, "CreateFile", [this, fileName, content, processIdentifier]() {
					this->createFile(fileName, content, false, processIdentifier); // 创建文件
				}, processIdentifier));
			} else if (command == 2) { // 删除文件
				std::string fileName, processIdentifier;
				int priority = 5; // 默认优先级为5
				iss >> fileName >> priority >> processIdentifier;
				if (priority > 5) { // 如果优先级大于5
					safePrint("错误：优先级输入错误，优先级必须小于或等于5。请重新输入。\n"); // 输出错误信息
					continue; // 跳过当前操作
				}
				processManager.addProcess(Process(2, priority, "DeleteFile", [this, fileName, processIdentifier]() {
					this->deleteFile(fileName, processIdentifier); // 删除文件
				}, processIdentifier));
			} else if (command == 3) { // 查看文件内容
				std::string fileName, processIdentifier;
				int priority = 5; // 默认优先级为5
				iss >> fileName >> priority >> processIdentifier;
				if (priority > 5) { // 如果优先级大于5
					safePrint("错误：优先级输入错误，优先级必须小于或等于5。请重新输入。\n"); // 输出错误信息
					continue; // 跳过当前操作
				}
				processManager.addProcess(Process(3, priority, "ViewFile", [this, fileName, processIdentifier]() {
					this->viewFile(fileName, processIdentifier); // 查看文件内容
				}, processIdentifier));
			} else if (command == 4) { // 修改文件内容
				std::string fileName, content, processIdentifier;
				int priority = 5; // 默认优先级为5
				iss >> fileName >> content >> priority >> processIdentifier;
				if (priority > 5) { // 如果优先级大于5
					safePrint("错误：优先级输入错误，优先级必须小于或等于5。请重新输入。\n"); // 输出错误信息
					continue; // 跳过当前操作
				}
				processManager.addProcess(Process(4, priority, "ModifyFile", [this, fileName, content, processIdentifier]() {
					this->modifyFile(fileName, content, processIdentifier); // 修改文件内容
				}, processIdentifier));
			} else if (command == 5) { // 查看文件的指定盘块内容
				std::string fileName, processIdentifier;
				int blockIndex, priority = 5; // 默认优先级为5
				iss >> fileName >> blockIndex >> priority >> processIdentifier;
				if (priority > 5) { // 如果优先级大于5
					safePrint("错误：优先级输入错误，优先级必须小于或等于5。请重新输入。\n"); // 输出错误信息
					continue; // 跳过当前操作
				}
				processManager.addProcess(Process(5, priority, "ViewFileBlock", [this, fileName, blockIndex, processIdentifier]() {
					this->viewFileBlock(fileName, blockIndex, processIdentifier); // 查看文件的指定盘块内容
				}, processIdentifier));
			} else if (command == 6) { // 修改文件的指定盘块内容
				std::string fileName, content, processIdentifier;
				int blockIndex, priority = 5; // 默认优先级为5
				iss >> fileName >> blockIndex >> content >> priority >> processIdentifier;
				if (priority > 5) { // 如果优先级大于5
					safePrint("错误：优先级输入错误，优先级必须小于或等于5。请重新输入。\n"); // 输出错误信息
					continue; // 跳过当前操作
				}
				processManager.addProcess(Process(6, priority, "ModifyFileBlock", [this, fileName, blockIndex, content, processIdentifier]() {
					this->modifyFileBlock(fileName, blockIndex, content, processIdentifier); // 修改文件的指定盘块内容
				}, processIdentifier));
			} else if (command == 7) { // 查看FAT表
				int priority = 5; // 默认优先级为5
				iss >> priority;
				if (priority > 5) { // 如果优先级大于5
					safePrint("错误：优先级输入错误，优先级必须小于或等于5。请重新输入。\n"); // 输出错误信息
					continue; // 跳过当前操作
				}
				processManager.addProcess(Process(7, priority, "ViewFAT", [this]() {
					this->viewFAT(); // 查看FAT表
				}, "ViewFATProcess"));
			} else if (command == 8) { // 退出
				safePrint("退出文件系统。\n"); // 输出退出信息
				stopVisualization(); // 停止可视化
				processManager.stopScheduler(); // 停止调度器
				closeAllFiles(); // 关闭所有打开的文件
				schedulerThread.join(); // 等待调度器线程结束
				return;
			} else {
				safePrint("无效命令: " + std::to_string(command) + "\n"); // 输出无效命令信息
			}
		}
		
		// 恢复调度器线程
		processManager.startScheduler();
		
		// 关闭所有打开的文件
		closeAllFiles();
		
		safePrint("\n所有操作执行完毕。\n");
		safePrint("是否继续输入操作？(y/n): ");
		char choice;
		std::cin >> choice;
		std::cin.ignore();
		
		if (choice != 'y' && choice != 'Y') { // 如果用户选择不继续
			safePrint("退出文件系统。\n"); // 输出退出信息
			stopVisualization(); // 停止可视化
			processManager.stopScheduler(); // 停止调度器
			closeAllFiles(); // 关闭所有打开的文件
			schedulerThread.join(); // 等待调度器线程结束
			break;
		}
	}
}

// 主函数
int main() {
	FileSystem fs; // 创建文件系统对象
	fs.interactiveMenu(); // 启动交互菜单
	return 0;
}
