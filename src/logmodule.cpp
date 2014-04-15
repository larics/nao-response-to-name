/**
 * Author: Frano Petric
 * Version: 0.9
 * Date: 2.4.2014.
 */

#include "logmodule.hpp"
#include <iostream>
#include <fstream>
#include <alvalue/alvalue.h>
#include <alcommon/alproxy.h>
#include <alcommon/albroker.h>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread.hpp>
#include <boost/lambda/lambda.hpp>
#include <qi/log.hpp>
#include <althread/alcriticalsection.h>

struct Logger::Impl {
    boost::shared_ptr<AL::ALMemoryProxy> memoryProxy;

    Logger &module;

    boost::shared_ptr<AL::ALMutex> fCallbackMutex;
    boost::thread *t;
    boost::mutex outputFileLock;

    boost::system_time lastFace;
    boost::system_time lastCall;
    boost::system_time sessionStart;
    std::ofstream outputFile;

    int iteration;
    int faceCount;
    int childCount;
    /** Struct constructor */
    Impl(Logger &mod) : module(mod), fCallbackMutex(AL::ALMutex::createALMutex()) {
        try {
            memoryProxy = boost::shared_ptr<AL::ALMemoryProxy>(new AL::ALMemoryProxy(mod.getParentBroker()));
        }
        catch (const AL::ALError& e) {
            qiLogError("Logger") << "Error creating proxy to ALMemory" << e.toString() << std::endl;
        }
        /** Declare events generated by this module
          * Subscribe to StartSession event
          */
        try {
            memoryProxy->declareEvent("CallChild", "Logger");
            memoryProxy->declareEvent("EndSession", "Logger");
            memoryProxy->subscribeToEvent("StartSession", "Logger", "onStartLogger");
            childCount = 0;
        }
        catch (const AL::ALError& e) {
            qiLogError("Logger") << "Error setting up Logger" << e.toString() << std::endl;
        }

    }

    void log(std::string eventIdentifier, int value) {
        boost::system_time now = boost::get_system_time();
        boost::posix_time::time_duration duration = now - sessionStart;

        outputFileLock.lock();
        outputFile << eventIdentifier << "\t" << value << "\t" << duration.total_milliseconds()/1000.0 << "\n";
        outputFileLock.unlock();
    }

    void startLogger() {
        /** Prepare file */
        boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
        std::stringstream file;
        std::string filename;
        file << "/home/nao/naoqi/modules/" << now.date().year() << "_" << static_cast<int>(now.date().month())
                 << "_" << now.date().day() << "_" <<  now.time_of_day().hours() << now.time_of_day().minutes() << "_log.txt";
        filename = file.str();
        outputFileLock.lock();
        outputFile.open(filename.c_str(), std::ios::out);
        outputFileLock.unlock();

        /** Session start time */
        sessionStart = boost::get_system_time();
        iteration = 0;
        faceCount = 0;
        childCount++;
        /** Subscribe to events */
        try {
            memoryProxy->subscribeToEvent("FaceDetected", "Logger", "onFaceDetected");
            memoryProxy->subscribeToEvent("ChildCalled", "Logger", "onChildCalled");
            memoryProxy->subscribeToEvent("EndSession", "Logger", "onStopLogger");
        }
        catch (const AL::ALError& e) {
            qiLogError("Logger") << "Error subscribing to FaceDeteced" << e.toString() << std::endl;
        }

        /** Start scheduler thread */
        t = new boost::thread(boost::ref(*module.impl));
    }

    void stopLogger() {
        outputFileLock.lock();
        outputFile.close();
        outputFileLock.unlock();
        /** stop thread */
        t->interrupt();
        t->join();
        qiLogWarning("Logger") << "Thread exited" << std::endl;
        try {
            memoryProxy->unsubscribeToEvent("FaceDetected", "Logger");
        }
        catch (const AL::ALError& e) {
            qiLogError("Logger") << "Error unsubscribing from FaceDeteced" << e.toString() << std::endl;
        }
    }

    /** Scheduler thread */
    void operator()() {
        lastFace = boost::get_system_time();
        qiLogVerbose("Logger") << "Last face time " << lastFace << std::endl;
        while( true ) {
            try{
                if( iteration >= 1 && faceCount >=5) {
                    qiLogWarning("Logger") << "Hello from thread, you should end, child responded" << std::endl;
                    log("SE", 1);
                    memoryProxy->raiseEvent("EndSession", AL::ALValue(1));
                }
                boost::system_time now = boost::get_system_time();
                boost::posix_time::time_duration timeDiff = now - lastFace;
                long long sinceLastFace = timeDiff.total_milliseconds();
                timeDiff = now - lastCall;
                long long sinceLastCall = timeDiff.total_milliseconds();

                if( sinceLastFace >= 5000 && sinceLastCall >= 5000){
                    if( iteration < 3 ) {
                        qiLogWarning("Logger") << "Hello from thread, you should call the child" << std::endl;
                        log("CS", iteration+1);
                        faceCount = 0;
                        memoryProxy->raiseEvent("CallChild", AL::ALValue(1));
                        lastCall = boost::get_system_time();
                    }
                    else if( iteration == 3 ) {
                        qiLogWarning("Logger") << "Hello from thread, you should use special phrase" << std::endl;
                        log("PS", iteration);
                        faceCount = 0;
                        memoryProxy->raiseEvent("CallChild", AL::ALValue(2));
                        lastCall = boost::get_system_time();
                    }
                    else {
                        qiLogWarning("Logger") << "Hello from thread, you should end" << std::endl;
                        log("SE", -1);
                        memoryProxy->raiseEvent("EndSession", AL::ALValue(-1));
                    }
                }
                boost::this_thread::sleep(boost::posix_time::milliseconds(100));
            }
            catch(boost::thread_interrupted&) {
                return;
            }
        }
    }
};

Logger::Logger(boost::shared_ptr<AL::ALBroker> pBroker, const std::string& pName) :  AL::ALModule(pBroker, pName) {

    setModuleDescription("Log module");

    functionName("onFaceDetected", getName(), "ShitMethod");
    BIND_METHOD(Logger::onFaceDetected);

    functionName("onStartLogger", getName(), "Other shit method");
    BIND_METHOD(Logger::onStartLogger);

    functionName("onStopLogger", getName(), "Yet another shit method");
    BIND_METHOD(Logger::onStopLogger);
    functionName("onChildCalled", getName(), "Yet another shit method");
    BIND_METHOD(Logger::onChildCalled);
}

Logger::~Logger() {
    /** Destructor code */
    /** Do cleanup */
}

void Logger::init() {
    /** Overriding ALModule::init */
    try {
        impl = boost::shared_ptr<Impl>(new Impl(*this));
        AL::ALModule::init();
    }
    catch (const AL::ALError& e) {
        qiLogError("Logger") << e.what() << std::endl;
    }
    qiLogVerbose("Logger") << "Logger initialized" << std::endl;
}

void Logger::onFaceDetected() {
    /**
      * As long as this is defined, the code is thread-safe.
      */

    AL::ALValue face = impl->memoryProxy->getData("FaceDetected");

    AL::ALCriticalSection section(impl->fCallbackMutex);
    impl->memoryProxy->unsubscribeToEvent("FaceDetected", "Logger");
    impl->lastFace = boost::get_system_time();
    /**
      * Check that face is detected
      */
    if( face.getSize() < 2 ) {
        qiLogWarning("Logger") << "Face not detected, size " << face.getSize() << std::endl;
    }
    else {
        qiLogInfo("Logger") << "Face detected, size " << face.getSize() << std::endl;
        impl->log("FD", ++impl->faceCount);
    }
    impl->memoryProxy->subscribeToEvent("FaceDetected", "Logger", "onFaceDetected");
}

void Logger::onStartLogger() {
    /** Thread safety */
    AL::ALCriticalSection section(impl->fCallbackMutex);
    impl->memoryProxy->unsubscribeToEvent("StartSession", "Logger");
    qiLogWarning("Logger") << "Starting logger, session no. " << impl->childCount+1 << std::endl;

    /** Start logger */
    impl->startLogger();

    /** Subscribe to ChildCalled event */
    impl->memoryProxy->subscribeToEvent("ChildCalled", "Logger", "onChildCalled");
}

void Logger::onStopLogger(const std::string &key, const AL::ALValue &value, const AL::ALValue &msg) {
    AL::ALCriticalSection section(impl->fCallbackMutex);
    impl->memoryProxy->unsubscribeToEvent("EndSession", "Logger");
    /** stop thread */
    impl->t->interrupt();
    impl->t->join();
    qiLogWarning("Logger") << "Thread exited" << std::endl;
    try {
        impl->memoryProxy->unsubscribeToEvent("FaceDetected", "Logger");
        impl->memoryProxy->subscribeToEvent("StartSession", "Logger", "onStartLogger");
    }
    catch (const AL::ALError& e) {
        qiLogError("Logger") << "Error managing events" << e.toString() << std::endl;
    }
    impl->outputFileLock.lock();
    impl->outputFile.close();
    impl->outputFileLock.unlock();

}

void Logger::onChildCalled(const std::string &key, const AL::ALValue &value, const AL::ALValue &msg) {
    AL::ALCriticalSection section(impl->fCallbackMutex);
    impl->memoryProxy->unsubscribeToEvent("ChildCalled", "Logger");
    /** Update the time of the last call **/
    impl->lastCall = boost::get_system_time();
    /** Increase iteration number */
    qiLogWarning("Logger") << "Child called, increase iteration to " << impl->iteration+1 << std::endl;
    impl->iteration++;
    impl->log("CE", (int)value);
    impl->memoryProxy->subscribeToEvent("ChildCalled", "Logger", "onChildCalled");
}