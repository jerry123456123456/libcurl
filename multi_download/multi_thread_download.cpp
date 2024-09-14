#include<iostream>
#include<string>
#include<unistd.h>
#include<errno.h>
#include<pthread.h>
#include<curl/curl.h>
#include<cstring>
#include<fcntl.h>
#include<map>
#include<sys/mman.h>

using namespace std;

#define THREAD_NUM  20

struct tNode{
    double download;   //当前线程下载的字节数
    pthread_t selfid;  //当前线程的id
    size_t startPos;   //当前线程下载的数据范围
    size_t endPos;
    char *ptr;      //mmap映射的内存指针
    string url;     //下载的Url地址
};

struct tNode **pNodeTable = NULL;  //存储线程信息的数组
double downloadFileLength = 0;     //文件的长度

//ptr是实际下载的数据
static size_t writeFunc(void *ptr,size_t size,size_t nmemb,void *userdata){
    tNode *node=(tNode *)userdata;
    size_t written=0;
    //检查下载的数据是否超过了当前线程负责的范围
    if((node->startPos + size*nmemb)<=node->endPos){
        memcpy(node->ptr+node->startPos,ptr,size*nmemb);
        node->startPos+=size*nmemb;
        written=size*nmemb;
    }else{
        if(node->startPos+size*nmemb>node->endPos+1){
            written=node->endPos -node->startPos+1;
        }else {
            written=size*nmemb;
        }
        memcpy(node->ptr+node->startPos, ptr,written);
        node->startPos+=written;
    }

    return written;
}

//实时更新下载进度的回调函数
int progressFunc(void *ptr,double totalToDownload,double nowDownloaded){
    int percent=0;
    static int print=1;  //每1%打印一次
    struct tNode *pNode=(struct tNode *)ptr;
    pNode->download=nowDownloaded;

    if(totalToDownload>0){
        double totalDownload=0;
        for(int i=0;i<THREAD_NUM;i++){
            if(pNodeTable[i]!=NULL){
                totalDownload+=pNodeTable[i]->download;   //累加所有线程下载的总字节数
            }
            percent=(int)(totalDownload/downloadFileLength *100);
        }
    }

    if(percent == print) {
		//pthread_t selfid = *(pthread_t*)ptr;
	    printf ("worker %ld : downloading: %0d%%\n", pNode->selfid, percent);
		print += 1;
	}
	return 0;
}

void *worker(void *pData){
    struct tNode *pNode=(struct tNode *)pData;
    pNode->selfid=pthread_self();

init:
    CURL *curl=curl_easy_init();

    char range[64]={0};
    snprintf (range, sizeof (range), "%ld-%ld", pNode->startPos, pNode->endPos);

    printf("selfid: %ld,range: %s\n",pNode->selfid,range);
    curl_easy_setopt(curl,CURLOPT_URL,pNode->url.c_str());          // 设置请求的 URL 地址
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,writeFunc);         //设置用于处理接收到的数据的回调函数
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,(void *)pNode);         //设置传递给 `writeFunc` 的用户数据
    curl_easy_setopt (curl, CURLOPT_NOPROGRESS, 0L);                // 设置是否启用进度显示。`0L` 表示启用

    curl_easy_setopt(curl,CURLOPT_PROGRESSDATA,pNode);              //设置传递给进度回调函数的用户数据
    curl_easy_setopt (curl, CURLOPT_PROGRESSFUNCTION, progressFunc);//设置用于处理进度信息的回调函数
    curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, 20L);           //设置连接超时时间，单位为秒。在这里设置为 20 秒
    curl_easy_setopt (curl, CURLOPT_TIMEOUT, 1200L);                //设置请求超时时间，单位为秒。在这里设置为 1200 秒
	curl_easy_setopt (curl, CURLOPT_NOSIGNAL, 1L);                  //禁用信号发送，这可以在多线程环境中更安全地使用 libcurl
    curl_easy_setopt (curl, CURLOPT_LOW_SPEED_LIMIT, 1L);           //设置低速限速，如果下载速度低于该值，libcurl 将尝试重新连接或中止下载
	curl_easy_setopt (curl, CURLOPT_LOW_SPEED_TIME, 500L);          //设置低速限速触发时间，单位为秒。如果下载速度低于 `CURLOPT_LOW_SPEED_LIMIT`，并持续低于该速度 `CURLOPT_LOW_SPEED_TIME` 秒，则 libcurl 将尝试重新连接或中止下载
    curl_easy_setopt (curl, CURLOPT_RANGE, range);                  //设置 HTTP 请求的范围，用于断点续传等功能
    curl_easy_setopt (curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/115.0.0.0 Safari/537.36");
    //设置请求的用户代理标识，模拟浏览器的标识发送请求。这些选项设置了 libcurl 发送 HTTP 请求时的各种参数，包括 URL、数据处理回调、超时设置、断点续传等功能

    CURLcode res = curl_easy_perform (curl);
 
	if (res != CURLE_OK)
	{
		if (errno != 0) goto init;
		//printf ("thred %ld, res: %d , errno: %d\n", pNode->selfid, res, errno);
	}

	//curl_slist_free_all(headers);
	curl_easy_cleanup (curl);

	//printf ("thred %ld exit\n", pNode->selfid);

	return pNode;
}

long getDownloadFileLength(const char *url) {
    CURL *handle = curl_easy_init();
    if (!handle) {
        std::cerr << "Failed to initialize CURL handle." << std::endl;
        return -1;
    }

    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/115.0.0.0 Safari/537.36");
    curl_easy_setopt(handle, CURLOPT_HEADER, 1);
    curl_easy_setopt(handle, CURLOPT_NOBODY, 1);

    CURLcode res = curl_easy_perform(handle);
    if (res == CURLE_OK) {
        double length = -1;
        res = curl_easy_getinfo(handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &length);
        if (res == CURLE_OK && length >= 0) {
            downloadFileLength = length;
        } else {
            std::cerr << "Failed to get content length from CURL or length is negative." << std::endl;
            downloadFileLength = -1;
        }
    } else {
        std::cerr << "CURL perform failed: " << curl_easy_strerror(res) << std::endl;
        downloadFileLength = -1;
    }

    curl_easy_cleanup(handle);
    return downloadFileLength;
}


bool download(int threadNum, string Url, string Path, string fileName) {
    long fileLength = getDownloadFileLength(Url.c_str());
    if (fileLength <= 0) {
        std::cerr << "Failed to get file length." << std::endl;
        return false;
    }

    const string outFileName = Path + fileName;
    int fd = open(outFileName.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        std::cerr << "Failed to open file." << std::endl;
        return false;
    }

    // Set the file size
    if (lseek(fd, fileLength - 1, SEEK_SET) == -1) {
        std::cerr << "Failed to set file size" << std::endl;
        close(fd);
        return false;
    }
    if (write(fd, "", 1) != 1) {
        std::cerr << "Failed to write data" << std::endl;
        close(fd);
        return false;
    }

    char *ptr = static_cast<char*>(mmap(nullptr, fileLength, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (ptr == MAP_FAILED) {
        std::cerr << "Failed to mmap file" << std::endl;
        close(fd);
        return false;
    }

    if (threadNum > THREAD_NUM) {
        std::cerr << "threadNum exceeds THREAD_NUM limit" << std::endl;
        munmap(ptr, fileLength);
        close(fd);
        return false;
    }

    long partSize = fileLength / threadNum;
    struct tNode *node[THREAD_NUM] = {0};
    pNodeTable = node;
    for (int i = 0; i < threadNum; i++) {
        node[i] = new tNode;
        node[i]->startPos = i * partSize;
        node[i]->endPos = (i == threadNum - 1) ? fileLength - 1 : (i + 1) * partSize - 1;
        node[i]->ptr = ptr;
        node[i]->url = Url;
    }

    pthread_t tid[THREAD_NUM];
    for (int i = 0; i < threadNum; i++) {
        pthread_create(&tid[i], NULL, worker, node[i]);
    }

    for (int i = 0; i < threadNum; i++) {
        pthread_join(tid[i], NULL);
    }

    for (int i = 0; i < threadNum; i++) {
        delete node[i];
    }

    munmap(ptr, fileLength);
    close(fd);

    std::cout << "Download succeeded..." << std::endl;
    return true;
}


// https://releases.ubuntu.com/22.04/ubuntu-22.04.5-live-server-amd64.iso.zsync
int main (int argc, char *argv[]) {

	curl_global_init(CURL_GLOBAL_ALL);

	download(THREAD_NUM,
        "https://releases.ubuntu.com/22.04/ubuntu-22.04.5-live-server-amd64.iso.zsync", //"https://releases.ubuntu.com/22.04/ubuntu-22.04.2-live-server-amd64.iso",
        "./",
        "server-amd64.iso.zsync");

	curl_global_cleanup();

	return 0;

}
