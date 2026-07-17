#ifndef SINGLETON_H
#define SINGLETON_H

#include <memory>
#include <mutex>

template <typename T>
class Singleton
{
public:
    ~Singleton() = default;

    static std::shared_ptr<T> GetInstance() {
        static std::once_flag s_flag;
        std::call_once(s_flag, [&](){
            m_instance = std::shared_ptr<T>(new T);
        });
        return m_instance;
    }
protected:
    Singleton() = default;
    Singleton (const Singleton& other) = delete;
    Singleton operator = (const Singleton& other) = delete;

    static std::shared_ptr<T> m_instance;

private:

};

template <typename T>
std::shared_ptr<T> Singleton<T>::m_instance = nullptr;

#endif // SINGLETON_H
