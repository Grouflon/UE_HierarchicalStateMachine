# TODO:
- Transitions Between Tracks and States
- Bump Events (if an event happens during a State, it is just added back to the queue)
- Make the state machine definition a shared asset
- Precompute route between states defines by events (no more route search at runtime)

# Usage Example

### Declaration
```C++
UPROPERTY(Instanced) UHierarchicalStateMachine* m_stateMachine;
```
### Definition
```C++
m_stateMachine = CreateDefaultSubobject<UHierarchicalStateMachine>(TEXT("stateMachine"));
STATEMACHINE_DEFINITION(m_sstateMachine)
(
  TRACK(RootTrack) // First level of the state machine must contain Tracks and not States. It can contain several Tracks
  (
    // Default state is just like any state, except it will be the state that 
    // is entered in the track if the transition does not define a specific destination state
    DEFAULT_STATE(State1) 
    (
      STATE_ENTER(this, &MyClass::State1_Enter);  // this is just delegates to you class methods
      STATE_TICK(this, &MyClass::State1_Tick);    // function prototype for TICK is void(float) the parameter is dt
      STATE_EXIT(this, &MyClass::State1_Exit);    // function prototype for ENTER/EXIT is void()
    );
        
    STATE(State2)
    (
      STATE_TICK(this, &MyClass::State2_Tick); //You can only declare the function you need between ENTER TICK & EXIT
          
      TRACK(SubTrack1) // Every State can have several tracks, Every Track can have several States
      (
        DEFAULT_STATE(SubState1)();
        STATE(SubState2)
        (
          // ...
        );
      );
          
      TRACK(SubTrack2)();
    );
  );
    
  // Every Track Name must be unique, every State Name must be unique as well

  TRANSITION_EVENT("EventName", SubState2, State1);
  TRANSITION_EVENT("EventName2", SubState2, SubState1); // Transition must declared from one state to another
);
```
# References
This state machine is greatly inspired and loosely adapted from [this work](http://www.wiwila.com/tools/phantom/documentation/state-machines/).
