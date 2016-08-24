#include "svc/authenticator/SVCAuthenticator.h"
#include "svc/SVC.h"
#include "svc/SVCApp.h"

class SendFileAppServer : SVCApp, SVCAuthenticator{

	public:
		SendFileAppServer();	
				
		//--	inherited interfaces
		std::string getAppID();
		std::string getIdentity();
		bool verifyIdentity(std::string identity, std::string challenge, std::string proof);
		std::string generateProof(std::string challenge);
		std::string generateChallenge();
		
};
