#ifndef __SVC_DAEMON_HEADER__
#define __SVC_DAEMON_HEADER__

	#include "MutexedQueue.h"

	#include <string>
	#include <iostream>
	#include <unordered_map>
	#include <unistd.h>
	#include <cstdlib>
	#include <csignal>
	#include <iostream>

	#include <sys/ipc.h>
	#include <sys/msg.h>
	#include <sys/socket.h>
	#include <sys/un.h>
	#include <sys/types.h>
	#include <netinet/in.h>

	#define SVC_VERSION 0x01<<6

	using namespace std;

	class DaemonEndPoint;
	class DaemonService;

	static unordered_map<uint64_t, DaemonService*> serviceTable;
	static shared_mutex* serviceTableMutex;

	struct sockaddr_un daemonSockUnAddress;
	struct sockaddr_in daemonSockInAddress;
	int daemonUnSocket;
	int daemonInSocket;

	pthread_t unixReadingThread;
	pthread_t htpReadingThread;
	pthread_attr_t htpReadingThreadAttr;
	pthread_attr_t unixReadingThreadAttr;
	uint8_t* htpReceiveBuffer;
	uint8_t* unixReceiveBuffer;

	string errorString;
	volatile bool working;


	class DaemonService{
	
		friend class DaemonEndPoint;
	
		bool isConnected;
		bool working;
	
		struct sockaddr_in sockAddr;
		socklen_t sockLen;
	
		unordered_map<uint64_t, DaemonEndPoint*> endPoints;
		shared_mutex* endPointsMutex;		
	
		//--	CRYPTO VARIABLES	--//
			
		int encryptMessage(const uint8_t* plainMessage, size_t plainLen, uint8_t* encryptedMessage, size_t* encryptedLen);		
		int decryptMessage(const uint8_t* encryptedMessage, size_t encryptedLen, uint8_t* plainMessage, size_t* plainLen);
	
		void sendData(const uint8_t* buffer, size_t bufferLen);
	
		public:
	
			uint32_t sessionID;
			uint32_t address;
		
			DaemonService(const struct sockaddr_in* sockaddr, socklen_t sockLen);
			~DaemonService();
		
			bool isWorking();		
			void stopWorking();
		
			DaemonEndPoint* addDaemonEndPoint(uint64_t endPointID, uint32_t appID);		
			DaemonEndPoint* getDaemonEndPoint(uint64_t endPointID);
	};

	class DaemonEndPoint{

		friend class DaemonService;
		private:
			pthread_attr_t threadAttr;
			pthread_t processIncomingThread;
			pthread_t processOutgoingThread;
			pthread_t sendInThread;
			pthread_t sendOutThread;
			bool working;
	
			DaemonService* daemonService;
			uint64_t endPointID;
			uint32_t appID;	
	
			struct sockaddr_un unSockAddr;			
			int unSock;
			
			static void* processingIncomingMessage(void* args);	
			static void* processingOutgoingMessage(void* args);
			static void* sendPacketToApp(void* args);
			static void* sendPacketOutside(void* args);

		public:	
			MutexedQueue<Message*>* incomingQueue;
			MutexedQueue<Message*>* outgoingQueue;
			MutexedQueue<Message*>* inQueue;
			MutexedQueue<Message*>* outQueue;
		
			DaemonEndPoint(DaemonService* daemonService, uint64_t endPointID, uint32_t appID);		
			~DaemonEndPoint();
		
			void stopWorking();				
	};

	void* unixReadingLoop(void* args);
	void* htpReadingLoop(void* args);


	//--	HELPER FUNCTIONS	--//
	DaemonService* getServiceByAddress(uint32_t address);
	DaemonService* getServiceBySessionID(uint32_t sessionID);
	void signal_handler(int sig);
	//---------------------------//
	
#endif
