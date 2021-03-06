/**********************************************************************
Copyright (C) <2014>  <Behrooz Shafiee Sarjaz>
This program comes with ABSOLUTELY NO WARRANTY;

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
**********************************************************************/

#include "FuseBFS.h"
#include <cstring>
#include <ctime>
#include <unistd.h>
#include "UploadQueue.h"
#include "DownloadQueue.h"
#include "Filenode.h"
#include "Filesystem.h"
#include "MemoryController.h"
#include "ZooHandler.h"
#include "LoggerInclude.h"
#include "Params.h"
#include "Statistics.h"
#include "SettingManager.h"
#include "Global.h"

using namespace std;

namespace BFS {

FileNode* createRootNode() {
  FileNode *node = new FileNode(FileSystem::delimiter, FileSystem::delimiter,true, false);
  unsigned long now = time(0);
  node->setCTime(now);
  node->setCTime(now);
  return node;
}

int bfs_getattr(const char *path, struct stat *stbuff) {
  if(DEBUG_GET_ATTRIB)
    LOG(DEBUG)<<"path=\""<<(path==nullptr?"null":path)<<"\", statbuf="<< stbuff;
  //Get associated FileNode*
  string pathStr(path, strlen(path));
  FileNode* node = FileSystem::getInstance().findAndOpenNode(pathStr);
  if (node == nullptr) {
    LOG(DEBUG)<<"Node not found: "<<path;
    return -ENOENT;
  }

  uint64_t inodeNum = FileSystem::getInstance().assignINodeNum((intptr_t)node);
  bool res = node->getStat(stbuff);
  //Fill Stat struct
  node->close(inodeNum);

  if(res)
  	return 0;
  else {
    LOG(ERROR)<<"Failed to read attrib for:"<<path;
  	return -ENOENT;
  }
}

int bfs_readlink(const char* path, char* buf, size_t size) {
  if(DEBUG_READLINK)
    LOG(DEBUG)<<"path=\""<<(path==nullptr?"null":path)<<"\", link=\""<<link<<"\", size="<<size;
  int res;
  res = readlink(path, buf, size - 1);
  if (res == -1)
    return -errno;
  buf[res] = '\0';
  return 0;
}

int bfs_mknod(const char* path, mode_t mode, dev_t rdev) {
  if(DEBUG_MKNOD)
    LOG(DEBUG)<<"path=\""<<(path==nullptr?"null":path)<<", mode="<<mode;
  int retstat = 0;
  //Check if already exist! if yes truncate it!
	string pathStr(path, strlen(path));
	FileNode* node = FileSystem::getInstance().findAndOpenNode(pathStr);
	if (node != nullptr)
	{
	  //Close it! so it can be removed if needed
    uint64_t inodeNum = FileSystem::getInstance().assignINodeNum((intptr_t)node);
    node->close(inodeNum);
    if(DEBUG_MKNOD)
      LOG(DEBUG)<<("Existing file! truncating to 0!");
		return bfs_ftruncate(path,0,nullptr);
	}

	string name = FileSystem::getInstance().getFileNameFromPath(path);
  if(!FileSystem::getInstance().nameValidator(name)) {
    if(DEBUG_MKNOD)
      LOG(ERROR)<<"Failure, can't create file: invalid name:"<<path;
    return -EINVAL;
  }

	//Not existing
  if (S_ISREG(mode)) {
    if(MemoryContorller::getInstance().getMemoryUtilization() < 0.9) {

      FileNode *newFile = FileSystem::getInstance().mkFile(pathStr,false,true);
      if (newFile == nullptr) {
        LOG(ERROR)<<"Failure, mkFile (newFile is nullptr)";
        return -ENOENT;
      }

      uint64_t inodeNum = FileSystem::getInstance().assignINodeNum((intptr_t)node);

      //File created successfully
      newFile->setMode(mode); //Mode
      unsigned long now = time(0);
      newFile->setMTime(now); //MTime
      newFile->setCTime(now); //CTime
      //Get Context
      struct fuse_context fuseContext = *fuse_get_context();
      newFile->setGID(fuseContext.gid);    //gid
      newFile->setUID(fuseContext.uid);    //uid

      if(DEBUG_MKNOD)
        LOG(DEBUG)<<"MKNOD:"<<(path==nullptr?"null":path)<<" created Locally, returning:"<<newFile;

      newFile->close(inodeNum);//It's a create and open operation
    } else {//Remote file
      if(DEBUG_MKNOD)
        LOG(DEBUG) <<"NOT ENOUGH SPACE, GOINT TO CREATE REMOTE FILE. UTIL:"<<MemoryContorller::getInstance().getMemoryUtilization();
      int tries = 3;
retryRemoteCreate:
      if(FileSystem::getInstance().createRemoteFile(pathStr))
        retstat = 0;
      else{
        LOG(ERROR) <<"Failure, create remote file:"<< pathStr<<" failed. try:"<<(3-tries+1);
        ZooHandler::getInstance().requestUpdateGlobalView();
        tries--;
        if(tries > 0)
          goto retryRemoteCreate;
        retstat = -EIO;
      }
    }
  } else {
    retstat = -EIO;
    LOG(ERROR)<<"Failure, expects regular file";
  }
  return retstat;
}

/** Create a directory
 *
 * Note that the mode argument may not have the type specification
 * bits set, i.e. S_ISDIR(mode) can be false.  To obtain the
 * correct directory type bits use  mode|S_IFDIR
 * */
int bfs_mkdir(const char* path, mode_t mode) {
  int retstat = 0;
  mode = mode | S_IFDIR;
  if(DEBUG_MKDIR)
    LOG(DEBUG)<<"path=\""<<(path==nullptr?"null":path)<<", mode="<<mode;

  if (S_ISDIR(mode)) {
    string pathStr(path, strlen(path));
    string name = FileSystem::getInstance().getFileNameFromPath(path);
    if(!FileSystem::getInstance().nameValidator(name)) {
      if(DEBUG_MKDIR)
        LOG(ERROR)<<"Can't create directory: invalid name:"<<path;
      return -EINVAL;
    }
    FileNode *newDir = FileSystem::getInstance().mkDirectory(pathStr,false);
    if (newDir == nullptr){
      LOG(ERROR)<<" mkdir (newDir is nullptr)";
      return -ENOENT;
    }
    //Directory created successfully
    newDir->setMode(mode); //Mode
    unsigned long now = time(0);
    newDir->setMTime(now); //MTime
    newDir->setCTime(now); //CTime
    //Get Context
    struct fuse_context fuseContext = *fuse_get_context();
    newDir->setGID(fuseContext.gid);    //gid
    newDir->setUID(fuseContext.uid);    //uid
  } else {
    retstat = -ENOENT;
    LOG(ERROR)<<"expects dir";
  }

  return retstat;
}

int bfs_unlink(const char* path) {
  if(DEBUG_UNLINK)
    LOG(DEBUG)<<"(path=\""<<(path==nullptr?"null":path)<<"\")";

  //Get associated FileNode*
  string pathStr(path, strlen(path));
  FileNode* node = FileSystem::getInstance().findAndOpenNode(pathStr);
  if (node == nullptr) {
    LOG(ERROR)<<"Node not found: "<<path;
    return -ENOENT;
  }

  //Close it! so it can be removed if needed
  uint64_t inodeNum = FileSystem::getInstance().assignINodeNum((intptr_t)node);

  if(node->isDirectory()){
    node->close(inodeNum);
    return -EISDIR;
  }


  node->close(inodeNum);
  LOG(DEBUG)<<"SIGNAL DELETE FROM UNLINK Key:"<<node->getFullPath()<<" isOpen?"<<node->concurrentOpen()<<" isRemote():"<<node->isRemote();
  if(!FileSystem::getInstance().signalDeleteNode(node,true)){
    LOG(ERROR)<<"DELETE FAILED FOR:"<<path;
    return -EIO;
  }
  //WHY?? I don't know why I added this!
  ///ZooHandler::getInstance().requestUpdateGlobalView();

  if(DEBUG_UNLINK)
  	LOG(DEBUG)<<"Removed "<<path;

  return 0;
}

int bfs_rmdir(const char* path) {
  if(DEBUG_RMDIR)
    LOG(DEBUG)<<"(path=\""<<(path==nullptr?"null":path)<<"\")";

  //Get associated FileNode*
  string pathStr(path, strlen(path));
  FileNode* node = FileSystem::getInstance().findAndOpenNode(pathStr);
  if (node == nullptr) {
    LOG(ERROR)<<"Node not found: "<< path;
    return -ENOENT;
  }

  uint64_t inodeNum = FileSystem::getInstance().assignINodeNum((intptr_t)node);

  if(node->childrenSize() > 0){//Non empty directory
    node->close(inodeNum);
    return -ENOTEMPTY;
  }

  LOG(DEBUG)<<"SIGNAL DELETE From RMDIR:"<<node->getFullPath();
  node->close(inodeNum);

  if(!FileSystem::getInstance().signalDeleteNode(node,true)){
    LOG(ERROR)<<"DELETE FAILED FOR:"<<path;
    return -EIO;
  }
  //WHY?? XXX
  ZooHandler::getInstance().requestUpdateGlobalView();

  return 0;
}
/*
int bfs_symlink(const char* from, const char* to) {
}*/

/**
 * TODO:
 * XXX: causes some applications like gedit to fail!
 * They depend on writing on a tempfile and then rename it to
 * the original file.
 */
int bfs_rename(const char* from, const char* to) {
  //if(DEBUG_RENAME)
  LOG(ERROR)<<"RENAME NOT IMPLEMENTED: from:"<<from<<" to="<<to;
  //Disabling rename
  return -EIO;
/*
  string oldPath(from, strlen(from));
  string newPath(to, strlen(to));
  string newName = FileSystem::getInstance().getFileNameFromPath(newPath);
  if(!FileSystem::getInstance().nameValidator(newName)) {
    if(DEBUG_RENAME)
      log_msg("\nbb_rename can't rename file: invalid name:\"%s\"\n", to);
    return EBFONT;
  }

  if(!FileSystem::getInstance().tryRename(oldPath,newPath)) {
    log_msg("\nbb_rename failed.\n");
    return -ENOENT;
  }
  else {
    if(DEBUG_RENAME)
      log_msg("bb_rename successful.\n");
    //FileNode* node = FileSystem::getInstance().getNode(newPath);
    //log_msg("bb_rename MD5:%s\n",node->getMD5().c_str());
    return 0;
  }
*/
}

/*int bfs_link(const char* from, const char* to) {
}*/

int bfs_chmod(const char* path, mode_t mode) {
  //Get associated FileNode*
  string pathStr(path, strlen(path));
  FileNode* node = FileSystem::getInstance().findAndOpenNode(pathStr);
  if(node == nullptr) {
    LOG(ERROR)<<"Failure, not found: "<<path;
    return -ENOENT;
  }
  //Assign inode number
  uint64_t inodeNum = FileSystem::getInstance().assignINodeNum((intptr_t)node);
  if(inodeNum == 0) {
    LOG(ERROR)<<"Error in assigning inodeNum, or opening file.";
    return -ENOENT;
  }
  //Update mode
  node->setMode(mode);
  node->close(inodeNum);
  return 0;
}

int bfs_chown(const char* path, uid_t uid, gid_t gid) {
  //Get associated FileNode*
  string pathStr(path, strlen(path));
  FileNode* node = FileSystem::getInstance().findAndOpenNode(pathStr);
  if(node == nullptr) {
    LOG(ERROR)<<"Failure, not found: "<<path;
    return -ENOENT;
  }
  //Assign inode number
  uint64_t inodeNum = FileSystem::getInstance().assignINodeNum((intptr_t)node);
  if(inodeNum == 0) {
    LOG(ERROR)<<"Error in assigning inodeNum, or opening file.";
    return -ENOENT;
  }
  //Update mode
  node->setGID(gid);
  node->setUID(uid);
  node->close(inodeNum);
  return 0;
}

int bfs_truncate(const char* path, off_t size) {
  if(DEBUG_TRUNCATE)
    LOG(DEBUG)<<"path=\""<<(path==nullptr?"null":path)<<"\", size:"<<size;
  return bfs_ftruncate(path,size,nullptr);
}

/*int bfs_utime(const char* path, struct utimbuf* ubuf) {
}*/


int bfs_open(const char* path, struct fuse_file_info* fi) {
  if(DEBUG_OPEN)
    LOG(DEBUG)<<"path=\""<<(path==nullptr?"null":path)<<"\", fi="<<fi<<" fh="<<fi->fh;
  //Get associated FileNode*
  string pathStr(path, strlen(path));
  FileNode* node = FileSystem::getInstance().findAndOpenNode(pathStr);
  if (node == nullptr) {
    LOG(ERROR)<<"Failure, cannot Open Node not found: "<<path;
    fi->fh = 0;
    return -ENOENT;
  }

  uint64_t inodeNum = FileSystem::getInstance().assignINodeNum((intptr_t)node);
  if(inodeNum!=0) {
		fi->fh = inodeNum;
		//LOG(ERROR)<<"ptr:"<<node;
		//LOG(ERROR)<<"OPEN:"<<node->getFullPath();
		return 0;
  }
  else {
    fi->fh = 0;
    LOG(ERROR)<<"Error in assigning inodeNum, or opening file.";
  	return -ENOENT;
  }
}

int bfs_read_error_tolerant(const char* path, char* buf, size_t size, off_t offset,
    struct fuse_file_info* fi){
  int retry = 1;
  int lastError = 0;
  while(retry > 0) {
    int res = bfs_read(path, buf, size, offset,fi);
    if (res != -EIO){
      return res;
    }
    LOG(ERROR)<<"Read failed for:"<<(path==nullptr?"null":path)<<" retrying:"<<(3-retry+1)<<" Time."<<" ErrorCode:"<<res;
    retry--;
    lastError = res;
  }
  return lastError;
}

int bfs_read(const char* path, char* buf, size_t size, off_t offset,
    struct fuse_file_info* fi) {
  if(DEBUG_READ)
    LOG(DEBUG)<<"path=\""<<(path==nullptr?"null":path)<<" size="<<
    size<<", offset="<<offset<<", fi="<<fi;
  //Handle path
  if(path == nullptr && fi->fh == 0) {
    LOG(ERROR)<<"fi->fh is null";
    return -ENOENT;
  }
  //Get associated FileNode*
  FileNode* node = (FileNode*) FileSystem::getInstance().getNodeByINodeNum(fi->fh);

  //Empty file
  if((!node->isRemote()&&node->getSize() == 0)||size == 0) {
    LOG(DEBUG)<<"Read from:path="<<node->getFullPath()<<", readSize="<<size<<", offset="<<offset<<", fileSize:"<<node->getSize();
    return 0;
  }
  long readBytes = 0;
  if(node->isRemote())
  	readBytes = node->readRemote(buf,offset,size);
  else
  	readBytes = node->read(buf,offset,size);

  if(readBytes >= 0) {
    if(DEBUG_READ)
      LOG(DEBUG)<<"Successful read from:path="<<node->getFullPath()<<" size="<<size<<", offset="<<offset;
    Statistics::reportRead(size);
    return readBytes;
  }
  else {
    LOG(ERROR)<<"Error in reading: path="<<node->getFullPath()<<" size="<<size<<", offset="<<offset<<" RetValue:"<<readBytes<<" nodeSize:"<<node->getSize()<<" isRemote?"<<node->isRemote();
    return -EIO;
  }
}
int bfs_write_error_tolerant(const char* path, const char* buf, size_t size, off_t offset,
    struct fuse_file_info* fi) {
  int retry = 3;
  int lastError = 0;
  while(retry > 0) {
    int res = bfs_write(path, buf, size, offset,fi);
    if (res != -EIO && res != -ENOSPC){
      if(res > 0)
    	  Statistics::reportWrite(size);
      return res;
    }
    LOG(ERROR)<<"write failed for:"<<(path==nullptr?"null":path)<<" retrying:"<<(3-retry+1)<<" Time."<<" ErrorCode:"<<res;
    retry--;
    lastError = res;
  }
  return lastError;
}
int bfs_write(const char* path, const char* buf, size_t size, off_t offset,
    struct fuse_file_info* fi) {
  if(DEBUG_WRITE)
    LOG(DEBUG)<<"path=\""<<(path==nullptr?"null":path)<<"\", buf="<<buf<<", size="<<
    size<<", offset="<<offset<<", fi="<<fi;
  //Handle path
  if (path == nullptr && fi->fh == 0) {
    LOG(ERROR)<<"\nbfs_write: fi->fh is null";
    return -ENOENT;
  }
  //Get associated FileNode*
  FileNode* node = (FileNode*) FileSystem::getInstance().getNodeByINodeNum(fi->fh);
  string fullPath = node->getFullPath();

  long written = 0;

  if (node->isRemote()){
    /**
     * -20 Transferring
     * -10 Successful Transfer
     * -2 No space left
     * -3 EIO
     */
    written = node->writeRemote(buf, offset, size);
    if(written == -10){//Transfer Successful
      //1)Open new file
      FileNode* newNode = FileSystem::getInstance().findAndOpenNode(fullPath);
      //2)Close the old node so it can be removed or whatever
      node->close(0);//Don't use fi->fh because we need this inode Nubmer
      //But don't assign a new inodeNum! we have an inode number from old one
      if (newNode == nullptr) {
        LOG(ERROR)<<"Failure, cannot Open New Node after Transfer: "<<fullPath;
        return -EIO;
      }
      //3)Replace inode with the new pointer if not local(not moved to here)
      if(newNode->isRemote())
        FileSystem::getInstance().replaceAllInodesByNewNode((intptr_t)node,(intptr_t)newNode);
      //4)Redo write
      return bfs_write(path,buf,size,offset,fi);
    } else if(written == -20){//Is transferring
retryTransferingNode:
      sleep(1);//sleep a little and try again;
      LOG(INFO) <<"SLEEPING FOR Transferring:"<<fullPath;
      //for TCP mode
#ifndef BFS_ZERO
      /**
       * Try to see if the file has been transferred or not!
       * if transferred the pointer should have change unless
       * it's being transferred to ourself(this node)
       */
      //1)Open new file
      FileNode* newNode = FileSystem::getInstance().findAndOpenNode(fullPath);
      //2)close old file
      if(newNode!=nullptr)
        node->close(0);//Don't use fi->fh because we need this inode Nubmer
      //But don't assign a new inodeNum! we have an inode number from old one
      if (newNode == nullptr) {
        LOG(ERROR)<<"Failure, cannot Open New Node after Transfer: "<<fullPath;
        sleep(1);
        goto retryTransferingNode;
      }
      /**
       * 3)Replace inode with the new pointer if not local(not moved to here)
       * Otherwise newNode and node are same and a transfer is
       * complete(newNode!=node)
       */
      if(newNode->isRemote() && newNode!=node)
        FileSystem::getInstance().replaceAllInodesByNewNode((intptr_t)node,(intptr_t)newNode);
      //4)Finally redo write(shared with ZERO_MODE)
#endif
      return bfs_write(path,buf,size,offset,fi);
    } else if(written == -40){//Inconsistency in global View! the remote node is not responsible
      LOG(ERROR)<<"Inconsistency in my globalView for:"<<node->getFullPath()<<" I think it is at:"<<node->getRemoteHostIP()<<" updating globalView...";
      ZooHandler::getInstance().requestUpdateGlobalView();
      FileNode* newNode = FileSystem::getInstance().findAndOpenNode(fullPath);
      if(newNode != node)
        FileSystem::getInstance().replaceAllInodesByNewNode((intptr_t)node,(intptr_t)newNode);

      return -EIO;
    } else if(written == -50){//Invalid offset
      LOG(ERROR)<<"Write failed for remoteFile: "<<node->getFullPath()<<" offset:"<<offset<<" remoteIP:"<<node->getRemoteHostIP();
      return -EIO;
    }
  }
  else {
    /**
     * @return
     * Failures:
     * -1 Moving
     * -2 NoSpace
     * -3 InternalError
     * Success:
     * >= 0 written bytes
     */
    FileNode* afterMove = nullptr;
    written = node->writeHandler(buf, offset, size, afterMove,true);
    if(afterMove) {//set fi->fh
      FileSystem::getInstance().replaceAllInodesByNewNode((intptr_t)node,(intptr_t)afterMove);
      node = afterMove;
    }
  }

  if (written != (long) size) {
    //LOG(ERROR)<<"Error in writing to:"<<node->getName()<< " IsRemote?"<<node->isRemote()<<" Code:"<<written;
    if (written == -1) //Moving
      //return -EAGAIN;
      return bfs_write(path,buf,size,offset,fi);
    else if(written == -2){ // No space
      LOG(ERROR)<<"No space left, error in writing to:"<<node->getName()<< " IsRemote?"<<node->isRemote()<<" Code:"<<written;
      return -ENOSPC;
    }
    else {//Internal IO Error
      LOG(ERROR)<<"Internal IO Error, error in writing to:"<<node->getName()<< " IsRemote?"<<node->isRemote()<<" Code:"<<written<<" offset:"<<offset;
      return -EIO;
    }
  }

  return written;
}

/*int bfs_statfs(const char* path, struct statvfs* stbuf) {
}*/

int bfs_flush(const char* path, struct fuse_file_info* fi) {
  if(DEBUG_FLUSH)
    LOG(DEBUG)<<"path=\""<<(path==nullptr?"null":path)<<"\", fi="<<fi;
  int retstat = 0;

  //Get associated FileNode*
  FileNode* node = (FileNode*)FileSystem::getInstance().getNodeByINodeNum(fi->fh);

  if(node->flush())
    return retstat;
  else{
    LOG(ERROR)<<"Flush failed for:"<<node->getFullPath();
    return -EIO;
  }
}

int bfs_release(const char* path, struct fuse_file_info* fi) {
  if(DEBUG_RELEASE)
    LOG(DEBUG)<<"path=\""<<(path==nullptr?"null":path)<<"\", fi="<<fi;
  int retstat = 0;
  //Handle path
  if(path == nullptr && fi->fh == 0) {
    LOG(ERROR)<<"fi->fh is null.";
    return ENOENT;
  }
  //Get associated FileNode*
  FileNode* node = (FileNode*)FileSystem::getInstance().getNodeByINodeNum(fi->fh);
  //Update modification time
  node->setMTime(time(0));
  //Node might get deleted after close! so no reference to it anymore!
  //Debug info backup
  string pathStr = node->getFullPath();
  //LOG(ERROR)<<"CLOSE: ptr:"<<node;
  //LOG(ERROR)<<node->getFullPath();

  //Now we can safetly close it!
  node->close(fi->fh);
  //we can earse ionode num from map as well
  //FileSystem::getInstance().removeINodeEntry(fi->fh);

  return retstat;
}

int bfs_fsync(const char* path, int isdatasynch, struct fuse_file_info* fi) {
  //Get associated FileNode*
  //FileNode* node = (FileNode*)fi->fh;
  //LOG(ERROR)<<"FSYNC not implemeted: path=\""<<(path==nullptr?"null":path)<<"\", fi="<<fi<<" isdatasynch:"<<isdatasynch;
  return bfs_flush(path,fi);
}

int bfs_setxattr(const char* path, const char* name, const char* value,
    size_t size, int flags) {
  LOG(ERROR)<<"SETXATTR not implemeted: path="<<(path==nullptr?"null":path)<<" name="<<name
      <<" value="<<value<<" size="<<size<<" flags="<<flags;
  return 1;
}

/*int bfs_getxattr(const char* path, const char* name, char* value,
    size_t size) {
}

int bfs_listxattr(const char* path, char* list, size_t size) {
}

int bfs_removexattr(const char* path, const char* name) {
}*/

int bfs_opendir(const char* path, struct fuse_file_info* fi) {
  if(DEBUG_OPENDIR)
    LOG(DEBUG)<<"path=\""<<(path==nullptr?"null":path)<<"\", fi="<<fi;
  //Get associated FileNode*
  string pathStr(path, strlen(path));
  FileNode* node = FileSystem::getInstance().findAndOpenNode(pathStr);
  if (node == nullptr) {
    LOG(ERROR)<<"Node not found: "<< path;
    fi->fh = 0;
    return -ENOENT;
  }

  uint64_t inodeNum = FileSystem::getInstance().assignINodeNum((intptr_t)node);
  if(inodeNum != 0) {
    fi->fh = inodeNum;
    return 0;
  } else {
    fi->fh = 0;
    LOG(ERROR)<<"Error in assigning inodeNum or openning dir.";
    return -EIO;
  }
}

int bfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
    off_t offset, struct fuse_file_info* fi) {
  if(DEBUG_READDIR)
    LOG(DEBUG)<<"path="<<(path==nullptr?"null":path)<<", buf="<<buf<<", filler="<<
    filler<<", offset="<<offset<<", fi="<<fi<<", fi->fh="<<fi->fh;
  //Get associated FileNode*
  FileNode* node = nullptr;
  //Handle path
  if(fi->fh == 0){
    LOG(ERROR)<<"fi->fh is null";
    return ENOENT;
  }

  //Get Node
  node = (FileNode*)FileSystem::getInstance().getNodeByINodeNum(fi->fh);
  if(node == nullptr){
    LOG(ERROR)<<"node is null";
    return -ENOENT;
  }

  return node->readDir(buf,(void*)filler);
}

int bfs_releasedir(const char* path, struct fuse_file_info* fi) {
  if(DEBUG_RELEASEDIR)
    LOG(DEBUG)<<"path=\""<<(path==nullptr?"null":path)<<"\", fi="<<fi;
  int retstat = 0;
  //Handle path
  if(path == nullptr && fi->fh == 0){
    LOG(ERROR)<<"fi->fh is null";
    return ENOENT;
  }

  //Get associated FileNode*
  FileNode* node = (FileNode*)FileSystem::getInstance().getNodeByINodeNum(fi->fh);
  //Update modification time
  node->setMTime(time(0));
  node->close(fi->fh);
  //we can earse ionode num from map as well
  //FileSystem::getInstance().removeINodeEntry(fi->fh);

  return retstat;
}

int bfs_fsyncdir(const char* path, int datasync, struct fuse_file_info* fi) {
  LOG(ERROR)<<"FSYNCDIR not implemeted path:"<<(path==nullptr?"null":path)<<", fi->fh:"<<fi->fh<<", isdatasynch:"<<datasync;
  return 0;
}

void* bfs_init(struct fuse_conn_info* conn) {
  //Initialize file system
  FileNode* rootNode = createRootNode();
  FileSystem::getInstance().initialize(rootNode);
  //Get Context
  struct fuse_context fuseContext = *fuse_get_context();
  rootNode->setGID(fuseContext.gid);
  rootNode->setUID(fuseContext.uid);
  //Log
  if(DEBUG_INIT)
    LOG(INFO)<<"Fuse Initialization";

  if(SettingManager::getBackendType()!=BackendType::NONE){
    LOG(INFO)<<"Starting SyncThreads";
    //Start SyncQueue threads
    UploadQueue::getInstance().startSynchronization();
    DownloadQueue::getInstance().startSynchronization();
    LOG(INFO)<<"SyncThreads running...";
  }

  if(SettingManager::runtimeMode()==RUNTIME_MODE::DISTRIBUTED){
    //Start Zoo Election
    ZooHandler::getInstance().startElection();
    LOG(INFO)<<"ZooHandler running...";
  }

  return nullptr;
}

/**
 * we just give all the permissions
 * TODO: we should not just give all the permissions
 */
int bfs_access(const char* path, int mask) {
  if(DEBUG_ACCESS)
    LOG(DEBUG)<<"path="<<(path==nullptr?"null":path)<<", mask="<<mask;
  //int retstat = R_OK | W_OK | X_OK | F_OK;
  int retstat = 0;
  return retstat;
}

/*int bfs_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
  if(S_ISREG(mode)) {
    if(bfs_open(path,fi) == 0)
      return 0;//Success
    dev_t dev = 0;
    if(bfs_mknod(path,mode,dev)==0)
      return bfs_open(path,fi);
  } else if(S_ISDIR(mode)){
    if(bfs_opendir(path,fi) == 0)
      return 0;
    if(bfs_mkdir(path,mode)==0)
      return bfs_opendir(path,fi);
  } else
    LOG(ERROR)<<"Failure, neither a directory nor a regular file.";

  LOG(ERROR)<<"Failure, Error in create.";
  return -EIO;
}*/

int bfs_ftruncate(const char* path, off_t size, struct fuse_file_info* fi) {
  if(DEBUG_TRUNCATE)
    LOG(DEBUG)<<"path="<<(path==nullptr?"null":path)<<", fi:"<<fi<<" newsize:"<<size;
  //Get associated FileNode*
  string pathStr(path, strlen(path));
  FileNode* node = FileSystem::getInstance().findAndOpenNode(pathStr);
  if (node == nullptr) {
    LOG(ERROR)<<"Error bfs_ftruncate: Node not found: "<<path;
    return -ENOENT;
  }
  else if(DEBUG_TRUNCATE)
    LOG(DEBUG)<<"Truncating:"<<(path==nullptr?"null":path)<<" from:"<<node->getSize()<<" to:"<<size<<" bytes";

  uint64_t inodeNum = FileSystem::getInstance().assignINodeNum((intptr_t)node);

  //Checking Space availability
  int64_t diff = size - node->getSize();
  if(diff > 0)
    if(!MemoryContorller::getInstance().checkPossibility(diff)) {
      LOG(ERROR)<<"Ftruncate failed(not enough space): "<<(path==nullptr?"null":path)<<" newSize:"<<node->getSize()<<" MemUtil:"<<
    		  MemoryContorller::getInstance().getMemoryUtilization()<<" TotalMem:"<<MemoryContorller::getInstance().getTotal()<<
			  " MemAvail:"<<MemoryContorller::getInstance().getAvailableMemory();
      node->close(inodeNum);
      return -ENOSPC;
    }


  bool res;
  if(node->isRemote())
    res = node->truncateRemote(size);
  else
    res = node->truncate(size);
  node->close(inodeNum);

  if(!res) {
    LOG(ERROR)<<"Ftruncate failed: "<<(path==nullptr?"null":path)<<" newSize:"<<node->getSize();
    return EIO;
  }
  else
    return 0;
}

/*int bfs_fgetattr(const char* path, struct stat* statbuf,
    struct fuse_file_info* fi) {
}

int bfs_lock(const char* arg1, struct fuse_file_info* arg2, int cmd,
    struct flock* arg4) {
}

int bfs_utimens(const char* path, const struct timespec tv[2]) {
}

int bfs_bmap(const char* arg1, size_t blocksize, uint64_t* idx) {
}

int bfs_ioctl(const char* arg1, int cmd, void* arg3,
    struct fuse_file_info* arg4, unsigned int flags, void* data) {
}

int bfs_poll(const char* arg1, struct fuse_file_info* arg2,
    struct fuse_pollhandle* ph, unsigned * reventsp) {
}

int bfs_write_buf(const char* arg1, struct fuse_bufvec* buf, off_t off,
    struct fuse_file_info* arg4) {
}

int bfs_read_buf(const char* arg1, struct fuse_bufvec** bufp, size_t size,
    off_t off, struct fuse_file_info* arg5) {
}

int bfs_flock(const char* arg1, struct fuse_file_info* arg2, int op) {
}
*/
int bfs_fallocate(const char* path, int mode, off_t offset, off_t length,
    struct fuse_file_info* fi) {
  return 0;
  /*if(DEBUG_FALLOCATE)
    LOG(DEBUG)<<"path="<<(path==nullptr?"null":path)<<", fi:"<<fi<<" offset:"<<offset<<" Length:"<<length;
  //Get associated FileNode*
  if(path == nullptr && fi->fh == 0) {
    LOG(ERROR)<<"fi->fh is null";
    return -ENOENT;
  }
  //Get associated FileNode*
  FileNode* node = (FileNode*) FileSystem::getInstance().getNodeByINodeNum(fi->fh);
  if (node == nullptr) {
    LOG(ERROR)<<"Error bfs_fallocate: Node not found: "<<path;
    return -ENOENT;
  }
  else if(DEBUG_FALLOCATE)
    LOG(DEBUG)<<"Fallocating:"<<"path="<<(path==nullptr?"null":path)<<", fi:"<<fi<<" offset:"<<offset<<" Length:"<<length;

  int result = 0;
  if(node->isRemote())
    result = -EAGAIN;
  else
    result = node->allocate(offset,length);

  if(result!=length)
    LOG(ERROR)<<"Ftruncate failed: "<<(path==nullptr?"null":path)<<" Size:"<<node->getSize();

  return result;*/
}

} //BFS namespace
