#ifndef FIXED_LIST_HPP_
#define FIXED_LIST_HPP_

#include <atomic>

template <typename T>
class CircBuf
{
	// don't use default ctor
	CircBuf();

	const int size;
	const int elm_len;
	T *data;
	int front;
	std::atomic<int> count;

  public:
	CircBuf(int, int);
	~CircBuf();
	void add(void *row);
	int len() { return count < size ? count + 1 : size; }
	T *operator[](int in_index);
};

template <typename T>
CircBuf<T>::CircBuf(int sz, int len) : size(sz), elm_len(len)
{
	if (sz == 0)
		throw std::invalid_argument("size cannot be zero");
	data = new T[sz];
	int i;
	for(i=0; i< sz; i++) {
		data[i].resize(elm_len);
	}
	count = -1;
}

template <typename T>
CircBuf<T>::~CircBuf()
{
	delete data;
}

// Thread safe
template <typename T>
void CircBuf<T>::add(void *row)
{
	// find index where insert will occur
	count++;
	int end = count % size;
	memcpy(&data[end][0], row, elm_len);
}

template <typename T>
T* CircBuf<T>::operator[](int in_index)
{
    int front;
    if(in_index >= size)
    {
        throw std::out_of_range("out of range");
    }
    if((count + 1) < size) 
        front = 0;
    else 
        front = (count + 1) % size;
    
	in_index = (in_index + front) % size;

    return &data[in_index];
}

#endif /* FIXED_LIST_HPP_ */