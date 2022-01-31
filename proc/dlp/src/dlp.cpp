#include "lib/dls/dls.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <string.h>
#include <string>
#include <thread>
#include <iostream>
#include <fstream>
#include <sys/time.h>
#include <csignal>
#include <vector>

#define MAX_LINES_PER_FILE 512  // limit text files to 512 lines
#define MAX_FILE_SIZE (1 << 23) // limit binary files to 2^23 bytes

using namespace dls;

bool verbose = false;
struct timeval curr_time;


// list of mqueues to close
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
std::vector<std::string> mqs;

void sighandler(int signum) {
    // close any open mqueues
    for(std::string mq : mqs) {
        mq_unlink(mq.c_str()); // hopefully this doesn't fail
        // if it does fail just move one, don't want to leave the process hanging
    }

    exit(signum);
}

void add_mq_to_close(std::string& mq) {
    pthread_mutex_lock(&lock);
    mqs.push_back(mq);
    pthread_mutex_unlock(&lock);
}

// from stack overflow https://stackoverflow.com/questions/3056307/how-do-i-use-mqueue-in-a-c-program-on-a-linux-based-system
#define CHECK(x) \
    do { \
        if (!(x)) { \
            fprintf(stderr, "%s:%d: ", __func__, __LINE__); \
            perror(#x); \
            exit(-1); \
        } \
    } while (0) \


// TODO write some printf errors to log file?
void read_queue(const char* queue_name, const char* outfile_name, bool binary) {
    unsigned int file_index = 0;
    std::string filename = outfile_name;

    mqd_t mq;
    struct mq_attr attr;
    char buffer[MAX_Q_SIZE + 1];

    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MAX_Q_SIZE;
    attr.mq_curmsgs = 0;

    mq = mq_open(queue_name, O_CREAT | O_RDONLY, 0644, &attr);
    CHECK((mqd_t)-1 != mq);

    std::string mq_name = queue_name;
    add_mq_to_close(mq_name);

    bool started = false;

    while(1) {
        std::ofstream file;

        if(binary) {
            file.open(filename.c_str(), std::ios::out | std::ios::trunc);
        } else {
            file.open(filename.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
        }

        if(!file.is_open()) {
            printf("Failed to open file: %s\n", outfile_name);
            exit(-1);
        }

        if(!started) {
            if(!binary) {
                gettimeofday(&curr_time, NULL);
                std::string timestamp = "[" + std::to_string(curr_time.tv_sec) + "."
                                        + std::to_string(curr_time.tv_usec) + "]";
                file << timestamp << " Starting DLP\n";
                file.flush();
            }

            started = true;
        }

        ssize_t read = -1;
        size_t writes = 0;
        size_t max = MAX_FILE_SIZE;

        if(!binary) {
            max = MAX_LINES_PER_FILE;
        }

        while(writes < max) {
            // gettimeofday(&curr_time, NULL);
            // std::string timestamp = "[" + std::to_string(curr_time.tv_sec) + "]";

            read = mq_receive(mq, buffer, MAX_Q_SIZE, NULL);
            if(read == -1) {
                printf("Read failed from MQueue: %s\n", queue_name);
            }

            if(!binary) {
                buffer[read] = '\0';
            }

            if(verbose) {
                if(binary) {
                    gettimeofday(&curr_time, NULL);
                    std::string timestamp = "[" + std::to_string(curr_time.tv_sec) + "]";
                    printf("%s received telemetry packet\n", timestamp.c_str());
                } else {
                    printf("%s\n", buffer);
                }
            }

            if(binary) {
                //file << timestamp;
                file.write(buffer, read);

                // count bytes for raw binary log files
                writes += read;
            } else {
                //file << timestamp << " " << buffer << '\n';
                file << buffer << '\n';

                // count lines for text log files
                writes++;
            }



            fflush(stdout);
            file.flush();
        }

        if(!binary) {
            file << "end of log file\n";
            file.flush();
        }

        file.close();

        file_index++;
        filename = outfile_name;
        filename += std::to_string(file_index);
    }
}

int main(int argc, char* argv[]) {
    // add signal handlers to close any open mqueues
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGSEGV, sighandler);
    signal(SIGFPE, sighandler);
    signal(SIGABRT, sighandler);

    if(argc > 1) {
        if(!strcmp(argv[1], "-v")) {
            printf("Running in verbose mode.\n\n");
            verbose = true;
        }
    }
    char* env = getenv("GSW_HOME");
    std::string gsw_home;
    if(env == NULL) {
        printf("Could not find GSW_HOME environment variable!\n \
                Did you run '. setenv'?\n");
        exit(-1);
    } else {
        size_t size = strlen(env);
        gsw_home.assign(env, size);
    }

    std::string msg_file = gsw_home + "/log/system.log";
    std::string tel_file = gsw_home + "/log/telemetry.log";

    std::thread m_thread(read_queue, MESSAGE_MQUEUE_NAME, msg_file.c_str(), false);
    std::thread t_thread(read_queue, TELEMETRY_MQUEUE_NAME, tel_file.c_str(), true);

    m_thread.join();
    t_thread.join();
}
