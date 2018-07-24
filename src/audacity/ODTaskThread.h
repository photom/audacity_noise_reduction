/**********************************************************************

  Audacity: A Digital Audio Editor

  ODTaskThread.h

  Created by Michael Chinen (mchinen) on 6/8/08
  Audacity(R) is copyright (c) 1999-2008 Audacity Team.
  License: GPL v2.  See License.txt.

******************************************************************//**

\class ODTaskThread
\brief A thread that executes a part of the task specfied by an ODTask.

*//*******************************************************************/





#ifndef __AUDACITY_ODTASKTHREAD__
#define __AUDACITY_ODTASKTHREAD__

#include <mutex>

class ODTaskThread {

};
class ODLocker {

};

//a wrapper for wxMutex.
class ODLock final : public std::mutex
{
public:
   ///Constructs a ODTaskThread
   ///@param task the task to be launched as an
   ODLock(){}
  virtual ~ODLock(){}
};

#endif
