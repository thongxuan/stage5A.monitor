#ifndef __SVC_QUEUE__
#define __SVC_QUEUE__

	#include "Node.h"
	#include <mutex>

	using namespace std;

	/* 
		Author: Immort
		This queue is implemented to be generic and thread-safe
	*/

	template <class T>
	class Queue{
	
		private:
			Node<T>* first;
			Node<T>* last;
			int count;			
			mutex countMutex;

		public:
	
			Queue(){
				this->first = NULL;
				this->last = NULL;
				this->count = 0;
			}
		
			~Queue(){
				while (this->notEmpty()){
					this->dequeue();
				}
			}
		
			bool notEmpty(){			
				bool rs;
				this->countMutex.lock();
				rs = count>0;
				this->countMutex.unlock();		
				return rs;
			}
		
			void enqueue(T data){
				Node<T>* element = new Node<T>();
				element->setData(data);
				element->setNext(NULL);
				this->countMutex.lock();
				if (this->count==0){
					this->first = element;
					this->last = element;
				}
				else{
					this->last->setNext(element);
					this->last = element;				
				}
				this->count++;
				this->countMutex.unlock();				
			}
		
			bool peak(T* data){
				bool rs;
				if (this->countMutex.try_lock()){
					if (this->count>0){
						*data = this->first->getData();
						rs = true;
					}
					else{
						rs = false;
					}
					this->countMutex.unlock();
				}
				else{
					rs = false;
				}
				return rs;				
			}
		
			void dequeue(){
				this->countMutex.lock();
				if (this->count>0){
					Node<T>* tmp = this->first;
					this->first = tmp->getNext();
					delete tmp;
					this->count--;	
				}
				/*
				else: queue is empty, do nothing
				*/
				this->countMutex.unlock();
			}			
	};

#endif
	