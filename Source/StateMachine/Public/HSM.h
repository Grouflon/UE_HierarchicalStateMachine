// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <Delegates/Delegate.h>

/**
 * 
 */
DECLARE_DELEGATE(StateBeginDelegate);
DECLARE_DELEGATE_OneParam(StateUpdateDelegate, float);
DECLARE_DELEGATE(StateEndDelegate);
DECLARE_DELEGATE_RetVal(bool, PollingTransitionDelegate);

/*

// == QUESTIONS ==
// - Comment on fait pour choisir dans quel State d'une track on commence ?
//		definir un etat comme etant celui par defaut ?
//			implique des conditions autres que evenementielles, vu qu'on aura peut etre raté les events si ils etaient non pertinents dans notre arbre precedent
//			on va fatalement transiter par cet etat par defaut, potentiellement pas grave, mais bruite un peu la comprehension
//		definir des transitions depuis la track, en plus de depuis l'etat
..			permettrait d'override l'etat par defaut si une transition se valide a l'entree dans le nouvel etat
//		sous la responsabilite de l'etat parent ?
			chiant, faut ecrire les choses deux fois, j'aime pas
//		

STATEMACHINE_DEFINITION(smPtr, MyClass)
(
	DEFAULT_STATE(alive)
	(
		TRACK(motion)
		(
			DEFAULT_STATE(grounded)();
			STATE(jumping)();
		);
		TRACK(health)
		(
			DEFAULT_STATE(healthy)();
			STATE(hurt)();
		);
	);
	STATE(dead)();

	TRANSITON_EVENT(alive, dead, onDeath);
	TRANSITION_CHECK(alive, dead, isDead);
	TRANSITION_EVENT_LOOP(grounded, jumping, onGroundStateChanged);
	TRANSITION_CHECK_LOOP(grounded, jumping, isTouchingGround);
);

*/

class HSMTest;

#define HSM_DEBUG 1

// NOTE: could probably be a component.
// See if there's something to gain from it
class HSM
{
	friend class HSMTest;

public:
	class Track;
	class State;

	friend class Track;
	friend class State;

	class Track
	{
		friend class HSM;
		friend class State;
	public:
		State* AddState(FName _name, const StateBeginDelegate& _begin, const StateUpdateDelegate& _update, const StateEndDelegate& _end);
		State* AddDefaultState(FName _name, const StateBeginDelegate& _begin, const StateUpdateDelegate& _update, const StateEndDelegate& _end);

	private:
		Track(FName _name, State* _parent, HSM* _hsm);
		~Track();

		FName m_name;
		TMap<FName, State*> m_states;
		State* m_parent = nullptr;
		State* m_defaultState = nullptr;
		HSM* m_hsm = nullptr;
	};

	class State
	{
		friend class HSM;
		friend class State;
	public:
		StateBeginDelegate Begin;
		StateUpdateDelegate Update;
		StateEndDelegate End;

		Track* AddTrack(FName _name);

		bool IsInTrack(const Track* _track);

	private:
		State(FName _name, Track* _parent, HSM* _hsm);
		~State();

		FName m_name;
		TMap<FName, Track*> m_tracks;
		Track* m_parent;
		HSM* m_hsm; 
	};

public:
	HSM();
	~HSM();

	Track* GetRootTrack();

	void AddEventTransition(FName _sourceStateName, FName _targetStateName, FName _eventName);
	void AddPollingTransition(FName _sourceStateName, FName _targetStateName, const PollingTransitionDelegate& _pollingDelegate, bool _negate = false);

	void Start();
	void Update(float _dt);
	void Stop();

	void PostEvent(FName _eventName);

private:
	void UnqueueEvents();
	void UpdatePollingEvents();
	Track* FindClosestCommonTrack(const State* _stateA, const State* _stateB);

	struct EventTransition
	{
		State* source = nullptr;
		State* target = nullptr;
	};

	struct PollingTransition
	{
		State* source = nullptr;
		State* target = nullptr;
		PollingTransitionDelegate pollingDelegate;
		bool negate = false;
	};

	Track m_root;
	TMap<FName, Track*> m_tracks;
	TMap<FName, State*> m_states;

	TArray<State*> m_currentStates; // Order in this array matters

	TMap<FName, TArray<EventTransition*>> m_eventTransitions;
	TArray<PollingTransition*> m_pollingTransitions;
	TArray<FName> m_eventsQueue;

	bool m_updating = false;
	bool m_started = false;

#if HSM_DEBUG
	struct HistoryEntry
	{
		FString log;
		State* state;
		EventTransition* event;
		PollingTransition* polling;
	};
	TArray<HistoryEntry> m_history;
#endif
};


// ==================
// DEFINITION HELPERS
// ==================

/*
#define STATEMACHINE_DEFINITION(HSMPointer, OwnerClass)\
	{\
	typedef OwnerClass _OwnerClass;\
	HSM* __hsm = HSMPointer;\
	TArray<HSM::Track*> __trackStack;\
	TArray<HSM::State*> __stateStack;\
	HSM::Track* __track;\
	HSM::State* __state;\
	__trackStack.Push(__hsm->GetRootTrack());\
	_STATEMACHINE_DEFINITION_CONTENT


#define _STATEMACHINE_DEFINITION_CONTENT(...)\
	__VA_ARGS__\
	}


#define DEFAULT_STATE(StateName)\
	{\
		StateBeginDelegate __begin;\
		StateUpdateDelegate __update;\
		StateEndDelegate __end;\
		__begin.BindRaw(this, &_OwnerClass::##StateName##_Begin);\
		__update.BindRaw(this, &_OwnerClass::##StateName##_Update);\
		__end.BindRaw(this, &_OwnerClass::##StateName##_End);\
		__state = __trackStack.Top()->AddDefaultState(TEXT(#StateName), __begin, __update, __end); \
		__stateStack.Push(__state);\
	}\
	_STATE_CONTENT


#define STATE(StateName)\
	{\
		StateBeginDelegate __begin;\
		StateUpdateDelegate __update;\
		StateEndDelegate __end;\
		__begin.BindRaw(this, &_OwnerClass::##StateName##_Begin);\
		__update.BindRaw(this, &_OwnerClass::##StateName##_Update);\
		__end.BindRaw(this, &_OwnerClass::##StateName##_End);\
		__state = __trackStack.Top()->AddState(TEXT(#StateName), __begin, __update, __end); \
		__stateStack.Push(__state);\
	}\
	_STATE_CONTENT


#define _STATE_CONTENT(...)\
	__VA_ARGS__\
	__stateStack.Pop()


#define TRACK(TrackName)\
	__track = __stateStack.Top()->AddTrack(TEXT(#TrackName));\
	__trackStack.Push(__track);\
	_TRACK_CONTENT


#define _TRACK_CONTENT(...)\
	__VA_ARGS__\
	__trackStack.Pop()


// NOTE: so far, I think event should be passed as string literals, since it will passed that way on the non-macro API
#define TRANSITION_EVENT(eventName, sourceState, targetState)\
	__hsm->AddEventTransition(#sourceState, #targetState, eventName)


#define _TRANSITION_POLLING(sourceState, targetState, pollingMethodName, negate)\
	{\
		PollingTransitionDelegate __d;\
		__d.BindRaw(this, &_OwnerClass::##pollingMethodName);\
		__hsm->AddPollingTransition(#sourceState, #targetState, __d, negate);\
	}


#define TRANSITION_POLLING(sourceState, targetState, pollingMethodName)\
	_TRANSITION_POLLING(sourceState, targetState, pollingMethodName, false)

#define TRANSITION_POLLING_LOOP(sourceState, targetState, pollingMethodName)\
	TRANSITION_POLLING(sourceState, targetState, pollingMethodName);\
	_TRANSITION_POLLING(sourceState, targetState, pollingMethodName, true)

	*/