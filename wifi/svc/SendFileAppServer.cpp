#include "SendFileAppServer.h"

//--TODO:	to be removed

using namespace std;

SVC* svcInstance;
volatile bool working;

void signal_handler(int signal){
	if (signal == SIGINT){
		printf("SIGINT caught, stop working\n");
		svcInstance->stopWorking();
		working = false;
	}
}

int main(int argc, char** argv){
	   
	working = true;
	SendFileAppServer* app = new SendFileAppServer();
	
	delete app;
}

SendFileAppServer::SendFileAppServer(){		
	svcInstance = new SVC(this, this);
	SVCEndPoint* endPoint;
	
	printf("waiting for connection request\n");
	do{
		try{
			endPoint = svcInstance->listenConnection();
			if (endPoint!=NULL){
				printf("client connected\n");
				//--	give todo work for this endPoint in new thread
			}
			else{
				printf("retry...\n");
			}
		}
		catch (const char* ex){
			printf("Error: %s\n", ex);
			signal_handler(SIGINT);
		}
	}
	while (working);
}

SendFileAppServer::~SendFileAppServer(){
	delete svcInstance;
}

//interface implementation

//--	SVCApp interface

string SendFileAppServer::getAppID(){
	return string("SEND_FILE_APP");
}

/*	SVCAuthenticator interface	*/

string SendFileAppServer::getIdentity(){
	return "IM_THE_SERVER";
}

bool SendFileAppServer::verifyIdentity(string identity, string challenge, string proof){
	return (identity.compare("IM_THE_CLIENT")==0 && challenge.append("OK").compare(proof)==0);
}

string SendFileAppServer::generateProof(string challenge){
	return challenge.append("OK");
}

string SendFileAppServer::generateChallenge(){
	return string("this can be another thing");
}
