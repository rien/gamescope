#pragma once

#include <mutex>

namespace gamescope
{
    template <typename T>
    class CLocked
    {
    public:
        CLocked( std::unique_lock lock, T& thing )
            : m_Lock{ std::move( lock ) }
            , m_Thing{ thing }
        {
        }

        operator       T&()       { return m_Thing; }
        operator const T&() const { return m_Thing; }

              T * operator ->()       { return &m_Thing; }
        const T * operator ->() const { return &m_Thing; }

              T & operator *()        { return m_Thing; }
        const T & operator *() const  { return m_Thing; }
    private:
        mutable std::unique_lock m_Lock;
        T& m_Thing;
    };
}
