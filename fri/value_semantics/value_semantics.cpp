/*
    Copyright 2012 Adobe Systems Incorporated
    Distributed under the MIT License (see license at
    http://stlab.adobe.com/licenses.html)
    
    This file is intended as example code and is not production quality.
*/

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cassert>

using namespace std;

/******************************************************************************/
// copy_on_write

/*
    Disclaimer:

    This is a simplified version of copy_on_write from the Adobe Source
    Libraries. It has not been well tested, thread safety has not been
    tested at all as my compiler doesn't support std::atomic<> yet.
    
    The version in ASL uses Intel TBB's atomic type and has been well
    tested.
*/

#if __has_feature(cxx_atomic)
#include <atomic>
using counter_t = std::atomic<size_t>;
#else
using counter_t = size_t;
#endif

/*
A copy-on-write wrapper for any model of a regular type.

Requirements:
    T models regular.

Copy-on-write sematics allow for object to be lazily copied - only creating a
copy when the value is modified and there is more than one reference to the
value.

Note:
    copy_on_write is thread safe when C++11 atomics are supported.

*/

template <typename T>   // models Regular
class copy_on_write
{
public:
    // The type of value stored.
    using value_type = T;

    /*
        The first call to the default constructor will construct a default
        instance of value_type which will be used for subsequent calls to the
        default constructor. The default instance will be released at exit.
    */
    copy_on_write()
    {
        /*
            NOTE : for thread safety this assumes static initization is
            thread safe - required in C++11. In non-compliant compilers
            use a once-init library.
        */
    
        static implementation_t default_s;
        object_m = default_s;
        ++object_m->count_m;
    }

    /*
        Constructs a new copy_on_write object with a value x.

        Paramter: x A default value to assign to this object
    */
    copy_on_write(T x) :
        object_m(new implementation_t(std::move(x)))
    { }

    /*
        Copy construction is a non-throwing operation and simply increments
        the reference count on the stored object.
    */
    copy_on_write(const copy_on_write& x) noexcept :
        object_m(x.object_m)
    {
        if (object_m) ++object_m->count_m;
    }

    copy_on_write(copy_on_write&& x) noexcept :
        object_m(x.object_m)
    {
        x.object_m = 0;
    }

    ~copy_on_write()
    {
        release();
    }

    /*
        As with copy construction, assignment is a non-throwing operation which
        releases the old value and increments the reference count of the item
        being assigned to.
    */
    copy_on_write& operator=(copy_on_write x) noexcept
    { *this = move(x); return *this; }


    copy_on_write& operator=(T x)
    {
        if (!object_m) object_m = new implementation_t(move(x));
        else if (object_m->count_m == size_t(1)) object_m->value_m = move(x);
        else reset(new implementation_t(move(x)));

        return *this;
    }

    /*
    Obtain a reference to the value the object is referencing. This will copy
    the underlying value (if necessary) so changes to the value do not affect
    other copy_on_write objects.

    Note that write() does not have the same preconditions as operator=().
    write() returns a reference to the underlying object's value, thus requiring
    that an underlying object exist. operator=() on the other hand will perform
    an allocation if one is necessary.

    Return: A reference to the underlying object
    */
    value_type& write()
    {
        assert(object_m && "FATAL : using a moved copy_on_write object");

        if (!(object_m->count_m == size_t(1)))
            reset(new implementation_t(object_m->value_m));

        return object_m->value_m;
    }

    /*!
    Obtain a const reference to the underlying object.

    Return: A const reference to the underlying object
    */
    const value_type& read() const noexcept
    {
        assert(object_m && "FATAL : using a moved copy_on_write object");
        return object_m->value_m;
    }

private:
    struct implementation_t
    {
        explicit implementation_t(T x) :
            value_m(std::move(x))
        { }

        counter_t   count_m = 1;
        value_type  value_m;
    };
    
    void release()
    {
        if (!object_m || --object_m->count_m) return;
        
        delete object_m;
    }

    void reset(implementation_t* to)
    {
        release();
        object_m = to;
    }

    implementation_t* object_m;
};

/******************************************************************************/
// Library

template <typename T>
void draw(const T& x, ostream& out, size_t position)
{ out << string(position, ' ') << x << endl; }

class object_t {
  public:
    template <typename T>
    object_t(const T& x) : object_(new model<T>(x))
    { }
    
    object_t(const object_t& x) : object_(x.object_->copy_())
    { cout << "copy" << endl; }
    object_t(object_t&& x) = default;
    object_t& operator=(object_t x)
    { object_ = move(x.object_); return *this; }
    
    friend void draw(const object_t& x, ostream& out, size_t position)
    { x.object_->draw_(out, position); }
    
  private:
    struct concept_t {
        virtual ~concept_t() = default;
        virtual concept_t* copy_() = 0;
        virtual void draw_(ostream&, size_t) const = 0;
    };
  
    template <typename T>
    struct model : concept_t {
        model(const T& x) : data_(x) { }
        concept_t* copy_() { return new model(*this); }
        void draw_(ostream& out, size_t position) const 
        { draw(data_, out, position); }
        
        T data_;
    };
    
   unique_ptr<concept_t> object_;
};

using document_t = vector<copy_on_write<object_t>>;

void draw(const document_t& x, ostream& out, size_t position)
{
    out << string(position, ' ') << "<document>" << endl;
    for (auto& e : x) draw(e.read(), out, position + 2);
    out << string(position, ' ') << "</document>" << endl;
}

using history_t = vector<document_t>;

void commit(history_t& x) { assert(x.size()); x.push_back(x.back()); }
void undo(history_t& x) { assert(x.size()); x.pop_back(); }
document_t& current(history_t& x) { assert(x.size()); return x.back(); }

/******************************************************************************/
// Client

class my_class_t {
    /* ... */
};

void draw(const my_class_t&, ostream& out, size_t position)
{ out << string(position, ' ') << "my_class_t" << endl; }

int main()
{
    history_t h(1);

    current(h).emplace_back(0);
    current(h).emplace_back(string("Hello!"));
    
    draw(current(h), cout, 0);
    cout << "--------------------------" << endl;
    
    commit(h);
    
    current(h).emplace_back(current(h));
    current(h).emplace_back(my_class_t());
    current(h)[1] = string("World");
    
    draw(current(h), cout, 0);
    cout << "--------------------------" << endl;
    
    undo(h);
    
    draw(current(h), cout, 0);
}
