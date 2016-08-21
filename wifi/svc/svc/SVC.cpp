#include "SVC.h"

//---for debugging, to be removed
#include <iostream>
#include <errno.h>
//---

using namespace std;

SVC::SVC(SVCApp* localApp, SVCAuthenticator* authenticator){

	hash<string> hasher;
	uint32_t sessionSecret;
	uint32_t sessionSecretResponded;
	vector<SVCCommandParam*> params;
	
	const char* errorString;
	
	this->sessionID = 0;
	this->authenticator = authenticator;
	this->localApp = localApp;
	
	sessionSecret = (uint32_t)hasher(to_string(rand()));
	
	//0. 	check for existed socket	
	uint32_t hashedID = (uint32_t)hasher(localApp->getAppID());
	this->svcClientPath = SVC_CLIENT_PATH_PREFIX + to_string(hashedID);
	
	int unlinkResult = unlink(this->svcClientPath.c_str());
	if (unlinkResult==0){
		errorString = SVC_ERROR_NAME_EXISTED;
		goto errorInit;
	}
	
	//1. 	create a socket to svc daemon
	//1.1 	daemon's endpoint
	memset(&this->daemonSocketAddress, 0, sizeof(this->daemonSocketAddress));
	this->daemonSocketAddress.sun_family = AF_LOCAL;
	memcpy(this->daemonSocketAddress.sun_path, SVC_DAEMON_PATH.c_str(), SVC_DAEMON_PATH.size());
	//1.2	svc's endpoint
	memset(&this->svcSocketAddress, 0, sizeof(this->svcSocketAddress));
	this->svcSocketAddress.sun_family = AF_LOCAL;
	memcpy(this->svcSocketAddress.sun_path, this->svcClientPath.c_str(), this->svcClientPath.size());
	//1.3 	create new diagram socket and bind to svc's endpoint
	this->svcSocket = socket(AF_LOCAL, SOCK_DGRAM, 0);	
	if (bind(this->svcSocket, (struct sockaddr*)&this->svcSocketAddress, sizeof(this->svcSocketAddress))==-1){
		errorString = SVC_ERROR_BINDING;
		goto errorInit;	
	}
	//1.4	connect to daemon
	this->svcDaemonSocket = socket(AF_LOCAL, SOCK_DGRAM, 0);
	connect(this->svcDaemonSocket, (struct sockaddr*) &this->daemonSocketAddress, sizeof(this->daemonSocketAddress));
	
	//1.5	create reading thread	
	sigset_t sig;
	sigfillset(&sig);
	sigaddset(&sig, SIGUSR2);
	sigaddset(&sig, SIGUSR1);
	if (pthread_sigmask(SIG_BLOCK, &sig, NULL)!=0){
		errorString = SVC_ERROR_CRITICAL;
		goto errorInit;
	}		
	this->working = true;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_create(&this->readingThread, &attr, processPacket, this);
	
	//2.	register localApp to daemon
	//2.1	send command to daemon
	params.push_back(new SVCCommandParam(4, (uint8_t*) &hashedID));
	params.push_back(new SVCCommandParam(4, (uint8_t*) &sessionSecret));
	sendCommand(this->svcDaemonSocket, this->sessionID, SVC_CMD_REGISTER_APP, &params);
	
	params.clear();
	if (!signalNotificator.waitCommand(SVC_CMD_REGISTER_APP, &params, SVC_DEFAULT_TIMEOUT)){
		errorString = SVC_ERROR_REQUEST_TIMEDOUT;
		goto errorInit;
	}	
	//2.3 get sessionID from response
	this->sessionID = *((uint32_t*)(params[0]->data));
	printf("sessionID: %08x\n", this->sessionID);
	sessionSecretResponded = *((uint32_t*)(params[1]->data));
	if (sessionSecretResponded == sessionSecret){
		//ok, this came from the daemon
		goto success;
	}
	else{
		//this is a fake response from elsewhere		
		goto errorInit;
	}
	
	errorInit:
		//destruct params manually
		params.clear();
		//unlink socket 
		this->destruct();
		throw errorString;
		
	success:
		params.clear();
}

void SVC::destruct(){
	this->working = false;	
	pthread_join(this->readingThread, NULL);
	//remove remaining notificator
	for (uint8_t cmd=0; cmd<_SVC_CMD_COUNT; cmd++){
		signalNotificator.removeNotificator((enum SVCCommand)cmd);
	}
	unlink(this->svcClientPath.c_str());
	cout<<"svc destructed\n";
}

SVC::~SVC(){
	//NOTE: may have to change to non-blocking read, otherwise the join could be
	//blocked by recv
	this->destruct();
}

/* SVC PRIVATE FUNCTION IMPLEMENTATION	*/

void* SVC::processPacket(void* args){
	
	int byteRead;
	uint8_t* buffer = (uint8_t*)malloc(SVC_DEFAULT_BUFSIZ);
	SVC* svcInstance = (SVC*)args;
	uint32_t sessionID;
	
	while (svcInstance->working){
		//read packet from svcSocket in blocking mode
		do{
			byteRead = recv(svcInstance->svcSocket, buffer, SVC_DEFAULT_BUFSIZ, MSG_DONTWAIT);
			//if (byteRead!=-1) printf("recv returned %d errno %d\n", byteRead, errno);
		}
		while((byteRead==-1) && svcInstance->working);
		
		if (byteRead>0){
			printf("read a packet: ");
			printBuffer(buffer, byteRead);			
			
			sessionID = *((uint32_t*)buffer);
			uint8_t infoByte = buffer[4];

			//printf("infoByte: %02x, after &: %02x\n", infoByte, infoByte & SVC_COMMAND_FRAME);	
			if (infoByte & SVC_COMMAND_FRAME){				
				enum SVCCommand cmd = (enum SVCCommand)buffer[5];				
				if ((sessionID==svcInstance->sessionID) || (cmd == SVC_CMD_REGISTER_APP)){								
					/*	call COMMAND handler	*/												
					SVCDataReceiveNotificator* notificator = signalNotificator.getNotificator(cmd);						
					if (notificator!=NULL){						
						//perform callback
						notificator->handler(buffer, byteRead, notificator);
						//remove handler
						signalNotificator.removeNotificator(cmd);
					}
					/*
					else: no notificator for this cmd on this list
					*/
				}
				/*
				else: invalid sessionID
				*/
			}
			else{
				/*	call DATA handler	*/
				svcInstance->dataHandler(buffer, byteRead, NULL);			
			}
		}
	}
	
	free(buffer);
}


/*	SVC PUBLIC FUNCTION IMPLEMENTATION	*/

bool SVC::establishConnection(SVCHost* remoteHost){
	this->host = remoteHost;

	//printf("host address in etablishConnection: %s\n", remoteHost->getHostAddress().c_str());
	vector<SVCCommandParam*> params;
	//authentication variables
	string identity;
	string challengeSent;
	string challengeReceived;
	string proof;
	
	//1.	send establishing request to the daemon with appropriate params
	//1.1	dertemine this is a client or server
	uint8_t svcRole = localApp->isServer()? SVC_ROLE_SERVER : SVC_ROLE_CLIENT;
	//1.2	add params
	params.clear();
	params.push_back(new SVCCommandParam(1, &svcRole));
	//1.3	add challenge from app and send to the daemon
	
	if (svcRole == SVC_ROLE_CLIENT){
		uint32_t serverAddress  = this->host->getHostAddress();
		//this app is the CLIENT, get challenge from localApp and send to daemon
		challengeSent = this->authenticator->generateChallenge();
		params.push_back(new SVCCommandParam(4, (uint8_t*)&serverAddress));		
		params.push_back(new SVCCommandParam(challengeSent.size(), (uint8_t*)challengeSent.c_str()));
	}
	
	sendCommand(this->svcDaemonSocket, this->sessionID, SVC_CMD_CONNECT_STEP1, &params);
	if (svcRole == SVC_ROLE_CLIENT){
		//a.	CLIENT BUSSINESS CODE
		//a.2	wait for SVC_CMD_CONNECT_STEP2, identity + proof + challenge, respectively. keyexchange is retained at daemon level.
		if (signalNotificator.waitCommand(SVC_CMD_CONNECT_STEP2, &params, SVC_DEFAULT_TIMEOUT)){
			//a2.1 get identity, proof, challenge
			identity = string((char*)params[0]);
			proof = string((char*)params[1]);
			challengeReceived = string((char*)params[2]);
			//a2.2 verify server's identity
			if (this->authenticator->verifyIdentity(identity, challengeSent, proof)){
				//a2.3 ok, server's identity verified. request daemon to perform keyexchange. the daemon responds the success of encrypting process
				params.clear();
				sendCommand(this->svcDaemonSocket, this->sessionID, SVC_CMD_CONNECT_STEP3, &params);
				//wait for daemon. from then on data are encrypted.
				if (signalNotificator.waitCommand(SVC_CMD_CONNECT_STEP3, &params, SVC_DEFAULT_TIMEOUT)){			
					//a.3 perform SVC_CMD_CONNECT_STEP4, send identity + proof
					params.clear();
					identity = this->authenticator->getIdentity();
					proof = this->authenticator->generateProof(challengeReceived);
					params.push_back(new SVCCommandParam(identity.size(), (uint8_t*)identity.c_str()));
					params.push_back(new SVCCommandParam(proof.size(), (uint8_t*)proof.c_str()));
					sendCommand(this->svcDaemonSocket, this->sessionID, SVC_CMD_CONNECT_STEP4, &params);
					
					//a.4 authenticated
					this->isAuthenticated = true;					
					return true;
				}
				else{
					//no response from daemon, error occured or timeout
					return false;
				}								
			}
			else{
				//server identity verification failed, exception
				return false;
			}
		}
		else{
			//timed out
			return false;
		}		
	}
	else{
		//b.	SERVER BUSINESS CODE
		//b2.	wait for SVC_CMD_CONNECT_STEP1 from the daemon, challenge from client. version is retained at daemon level.
		// if the call failed, may be the daemon rejected the packet because of not matching SVC version

		if (signalNotificator.waitCommand(SVC_CMD_CONNECT_STEP1, &params, SVC_DEFAULT_TIMEOUT)){
			//b.2.1	read the challenge, return identiy, proof, challenge. keyexchange will be inserted at daemon level			
			challengeReceived = string((char*)params[0]);			
			proof = this->authenticator->generateProof(challengeReceived);
			identity = this->authenticator->getIdentity();
			challengeSent = this->authenticator->generateChallenge();	//reuse this variable
			//b.2.2	prepare response
			params.clear();
			params.push_back(new SVCCommandParam(identity.size(), (uint8_t*)identity.c_str()));
			params.push_back(new SVCCommandParam(proof.size(), (uint8_t*)proof.c_str()));
			params.push_back(new SVCCommandParam(challengeSent.size(), (uint8_t*)challengeSent.c_str()));	
			//b.2.3 send response
			sendCommand(this->svcDaemonSocket, this->sessionID, SVC_CMD_CONNECT_STEP2, &params);
			
			//b.3. wait for SVC_CMD_CONNECT_STEP4, step3 is handled by the daemon
			params.clear();
			if (signalNotificator.waitCommand(SVC_CMD_CONNECT_STEP4, &params, SVC_DEFAULT_TIMEOUT)){
				//b3.1 read identity + proof
				identity = string((char*)params[0]);
				proof = string((char*)params[1]);
				
				//b3.2 verify client's identity
				this->isAuthenticated = this->authenticator->verifyIdentity(identity, challengeSent, proof);
				return this->isAuthenticated;
			}
			else{
				//timed out
				return false;
			}		
		}
		else{
			//timed out
			return false;
		}							
	}
	return true;
}


bool SVC::setDataReceiveHandler(SVCDataReceiveHandler handler){
	this->dataHandler = handler;
}

int SVC::sendData(const uint8_t* data, size_t datalen, uint8_t priority, bool tcp){
	if (this->isAuthenticated){
		size_t bufferLength = 9 + datalen;
		uint8_t* buffer = (uint8_t*)malloc(bufferLength);
		uint8_t infoByte = 0;
		
		if (tcp) infoByte = infoByte | SVC_USING_TCP;
		infoByte = infoByte | priority;		
		infoByte = infoByte | SVC_DATA_FRAME;
		
		memcpy(buffer, (uint8_t*)(&this->sessionID), sizeof(this->sessionID));
		buffer[4] = infoByte;		
		memcpy(buffer+5, (uint32_t*) &datalen, 4);
		memcpy(buffer+9, data, datalen);
	
		//send packet
		send(this->svcSocket, buffer, bufferLength, 0);	
		free(buffer);
	}
	else{
		throw SVC_ERROR_NOT_ESTABLISHED;
	}
}


