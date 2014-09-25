/*
 * ZooHandler.h
 *
 *  Created on: Sep 22, 2014
 *      Author: behrooz
 */

#ifndef ZOOHANDLER_H_
#define ZOOHANDLER_H_
#include <stdio.h>
#include <stdlib.h>
#include <zookeeper.h>
#include <proto.h>
#include <ctime>
#include <string>
#include <errno.h>
#include <vector>
#include <unistd.h>
#include "LeaderOffer.h"
#include "MasterHandler.h"
#include "ZooNode.h"


namespace FUSESwift {
/**
 * The internal state of the election support service.
 */
enum class ElectionState {
	START, OFFER, DETERMINE, LEADER, READY, FAILED, STOP
};

class ZooHandler {
  friend MasterHandler;
private:
	zhandle_t *zh;
	clientid_t myid;
	int sessionState;
	const std::string hostPort = "10.42.0.62:2181,10.42.0.62:2182,10.42.0.62:2183";
	//const std::string hostPort = "127.0.0.1:2181";
	const int connectionTimeout = 5000*1000;//wait up to 5 seconds for connection
	const std::string electionZNode = "/BFSElection";
	const std::string assignmentZNode = "/BFSTasks";
	const char nodeDelimitter = '/';
	ElectionState electionState;
	LeaderOffer leaderOffer;
	//A hashmap too keep track of which file is at which node!<nodeaddress,list of files>
	std::vector<ZooNode> globalView;
	//Private Constructor
	ZooHandler();
	/** Election private helpers **/
	static void sessionWatcher(zhandle_t *zzh, int type, int state, const char *path, void* context);
	bool connect();
	bool blockingConnect();
	bool makeOffer();
	bool determineElectionStatus();
	std::vector<LeaderOffer> toLeaderOffers(const std::vector<std::string> &children);
	void becomeLeader();
  void becomeReady(LeaderOffer neighborLeaderOffer);
  static void neighbourWatcher(zhandle_t *zzh, int type, int state, const char *path, void* context);
  /** Updates Global View of files at each node **/
  void updateGlobalView();
  /** Keeps an eye on the nodes, to see if there is a change in their file list **/
  static void nodeWatcher(zhandle_t *zzh, int type, int state, const char *path, void* context);
  /** Keeps an eye on the BFSElection znode to see if a new client joins. **/
  static void electionFolderWatcher(zhandle_t *zzh, int type, int state, const char *path, void* context);
public:
	static ZooHandler& getInstance();
	virtual ~ZooHandler();
	/** Helper methods **/
	static std::string sessiontState2String(int state);
	static std::string zooEventType2String(int state);
	static void dumpStat(const struct Stat *stat);
	static std::string getHostName();
	/** Zoo Operations **/
	int getSessionState();
	ElectionState getElectionState();
	void publishListOfFiles();
	const std::vector<ZooNode> & getGlobalView();
	void startElection();
};

}//Namespace
#endif /* ZOOHANDLER_H_ */