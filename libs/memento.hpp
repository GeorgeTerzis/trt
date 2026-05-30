#pragma once


template<typename T>
class memento{
  T& _ref; 
  T _state;

public:
  memento() = delete;
  memento(T& ref) : _ref(ref), _state(ref) {}

  T& val() {
    return _state;
  }
  
  void apply() {
    _ref = _state;
  }

  void reset(){
    _state = _ref;
  }

  const T& val() const {
    return _state;
  }
};


template<typename T>
auto make_memento(T& val){
  return memento<T>(val);
}


