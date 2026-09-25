#ifndef VEH_BASE_DAMAGE_H
#define VEH_BASE_DAMAGE_H

#include "core/output.h"
#include "core/types.h"
#include "vector/vector3.h"

class phImpactBase;
class phInst;
class bkBank;

class vehAuto;

class vehDamage {
public:
    vehDamage() : m_pVehicle(nullptr), m_Enabled(false) {}
    virtual ~vehDamage() {}

    // The base damage model holds only the enable flag: a vehicle put back on
    // the grid is undamaged and taking damage again.  Everything else (the
    // panels, the timers, the explosion) belongs to the subclass, so Update
    // and Impact are hooks with nothing of their own to do - which is what
    // they are in the original as well (veh_base.lib/damage.obj: Reset at
    // 0x514880 sets the byte at +0x8, Update at 0x514890 is a bare "jr ra").
    virtual void Reset() { m_Enabled = true; }
    virtual void Update() { }
    virtual void Impact(phImpactBase *impactList, phInst *hitInst) { (void)impactList; (void)hitInst; }
    virtual void AddDamage(float d) { (void)d; }

    bool IsEnabled() const { return m_Enabled; }
    void SetEnabled(bool on) { m_Enabled = on; }
    virtual void SetVehicle(vehAuto *v) { m_pVehicle = v; }
    vehAuto* GetVehicle() const { return m_pVehicle; }
#if __BANK
    virtual void AddWidgets(bkBank &bank) { Quitf("vehDamage::AddWidgets - not implemented"); }
#endif

protected:
    vehAuto *m_pVehicle;
    bool m_Enabled;
};

#endif // VEH_BASE_DAMAGE_H
