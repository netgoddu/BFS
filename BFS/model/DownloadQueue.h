/*
 * DownlaodQueue.h
 *
 *  Created on: 2014-07-17
 *      Author: Behrooz Shafiee Sarjaz
 */

#ifndef DOWNLOADQUEUE_H_
#define DOWNLOADQUEUE_H_
#include "SyncQueue.h"

namespace FUSESwift {

class DownloadQueue: public SyncQueue{
  //Singleton instance
  static DownloadQueue* mInstance;
  //Process Events
  void processEvent(SyncEvent* &_event);
  static void syncLoopWrapper();
  void syncLoop();
  void updateFromBackend();
  void processDownloadContent(SyncEvent* _event);
  void processDownloadMetadata(SyncEvent* _event);
  //Private constructor
  DownloadQueue();
public:
  static DownloadQueue* getInstance();
  virtual ~DownloadQueue();
  //Start Downlaod Thread
  void startSynchronization();
};

} /* namespace FUSESwift */
#endif /* DOWNLOADQUEUE_H_ */
