// motionhuman.h

#ifndef _MOTIONHUMAN_H_
#define	_MOTIONHUMAN_H_


class CInstanceManager;
class CEngine;
class CLight;
class CParticule;
class CTerrain;
class CCamera;
class CBrain;
class CPhysics;
class CObject;


#define MH_MARCH		0
#define MH_MARCHTAKE	1
#define MH_TURN			2
#define MH_STOP			3
#define MH_FLY			4
#define MH_SWIM			5
#define MH_SPEC			6

#define MHS_DRIVE1		0
#define MHS_DRIVE2		1
#define MHS_TAKE		2
#define MHS_TAKEOTHER	3
#define MHS_TAKEHIGH	4
#define MHS_UPRIGHT		5
#define MHS_WIN			6
#define MHS_LOST		7
#define MHS_DEADg		8
#define MHS_DEADg1		9
#define MHS_DEADg2		10
#define MHS_DEADg3		11
#define MHS_DEADg4		12
#define MHS_DEADw		13
#define MHS_FLAG		14
#define MHS_SATCOM		15


class CMotionHuman : public CMotion
{
public:
	CMotionHuman(CInstanceManager* iMan, CObject* object);
	~CMotionHuman();

	void	DeleteObject(BOOL bAll=FALSE);
	BOOL	Create(D3DVECTOR pos, float angle, ObjectType type, BOOL bPlumb);
	BOOL	EventProcess(const Event &event);
	Error	SetAction(int action, float time=0.2f);

protected:
	void	CreatePhysics(ObjectType type);
	BOOL	EventFrame(const Event &event);

protected:
	int			m_partiReactor;
	float		m_armMember;
	float		m_armTimeAbs;
	float		m_armTimeAction;
	float		m_armTimeSwim;
	short		m_armAngles[3*3*3*3*7 + 3*3*3*16];
	int			m_armTimeIndex;
	int			m_armPartIndex;
	int			m_armMemberIndex;
	int			m_armLastAction;
	BOOL		m_bArmStop;
	float		m_lastSoundMarch;
	float		m_lastSoundHhh;
	float		m_time;
	float		m_tired;
	BOOL		m_bDisplayPerso;
};


#endif //_MOTIONHUMAN_H_
