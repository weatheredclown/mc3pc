#ifndef BANK_BANK_H
#define BANK_BANK_H

#define BANK_VERSION 0x0200

#include "core/output.h"
#include "data/callback.h"
#include "core/types.h"
#include "vector/vector3.h"
#include "vector/vector2.h"
#include "atl/array.h"
#include <string>

// __BANK=0: the bank (RAG debug widget) UI is not part of the build, so the
// declarations below are compiled out and any code still reaching for a widget
// fails at compile time.  The includes above stay visible: headers that pick up
// core types through this one (e.g. mccar/colorlib.h and Base) must keep
// compiling without the bank.
#if __BANK

struct HWND__;
typedef struct HWND__ *HWND;

class bkWidget {
public:
    static int sm_ShownLineCount;   // rows of widgets the bank overlay shows
public:
    virtual ~bkWidget() {}
    virtual const char* GetName() const = 0;
    virtual void CreateWin32Control(HWND parent, int x, int y, int w, int &yOffset) = 0;
    virtual void OnCommand(unsigned short code) { Quitf("bkWidget::OnCommand - not implemented"); }
    virtual void OnScroll(int pos) { Quitf("bkWidget::OnScroll - not implemented"); }
    virtual void UpdateFromBound() { Quitf("bkWidget::UpdateFromBound - not implemented"); }
};

class bkSlider : public bkWidget {
public:
    virtual void SetValue(float val) = 0;
    virtual float GetValue() const = 0;
    virtual void SetRange(float min, float max) { Quitf("bkSlider::SetRange - not implemented"); }
};

class bkBank {
public:
    bkBank(const char* name, int x = 0, int y = 0);
    ~bkBank();

    // Deferred population (RegisterBank): the callback that adds the widgets
    // runs the first time the bank is shown, not at registration.  Game code
    // registers banks during startup, before the objects the widgets point at
    // (car part database, game state) exist.
    void SetPopulateCallback(const datCallback &cb);
    void EnsurePopulated();
    bool IsPopulated() const { return m_Populated; }

    bkSlider* AddSlider(const char* name, float* val, float min, float max, float step, const datCallback &cb = NullCB, const char* unit_or_desc = nullptr);
    bkSlider* AddSlider(const char* name, float* val, float min, float max, float step, const datCallback &cb, const char* unit, const char* description);

    bkSlider* AddSlider(const char* name, int* val, float min, float max, float step, const datCallback &cb = NullCB, const char* unit_or_desc = nullptr);
    bkSlider* AddSlider(const char* name, int* val, float min, float max, float step, const datCallback &cb, const char* unit, const char* description);

    bkSlider* AddSlider(const char* name, Vector3* val, float min, float max, float step, const datCallback &cb = NullCB, const char* unit_or_desc = nullptr);
    bkSlider* AddSlider(const char* name, Vector3* val, float min, float max, float step, const datCallback &cb, const char* unit, const char* description);

    bkSlider* AddSlider(const char* name, Vector2* val, float min, float max, float step, const datCallback &cb = NullCB, const char* unit_or_desc = nullptr);
    bkSlider* AddSlider(const char* name, Vector2* val, float min, float max, float step, const datCallback &cb, const char* unit, const char* description);

    bkSlider* AddSlider(const char* name, class Vector4* val, float min, float max, float step, const datCallback &cb = NullCB, const char* unit_or_desc = nullptr);
    bkSlider* AddSlider(const char* name, class Vector4* val, float min, float max, float step, const datCallback &cb, const char* unit, const char* description);

    bkSlider* AddSlider(const char* name, u32* val, float min, float max, float step, const datCallback &cb = NullCB, const char* unit_or_desc = nullptr);
    bkSlider* AddSlider(const char* name, u32* val, float min, float max, float step, const datCallback &cb, const char* unit, const char* description);

    // Narrow-integer sliders (mc3 tunes s8/u8/s16/u16 fields).
    bkSlider* AddSlider(const char* name, s8* val, float min, float max, float step, const datCallback &cb = NullCB, const char* unit_or_desc = nullptr);
    bkSlider* AddSlider(const char* name, u8* val, float min, float max, float step, const datCallback &cb = NullCB, const char* unit_or_desc = nullptr);
    bkSlider* AddSlider(const char* name, s16* val, float min, float max, float step, const datCallback &cb = NullCB, const char* unit_or_desc = nullptr);
    bkSlider* AddSlider(const char* name, u16* val, float min, float max, float step, const datCallback &cb = NullCB, const char* unit_or_desc = nullptr);
    void AddToggle(const char* name, bool* val, const datCallback &cb = NullCB, const char* description = nullptr);
    void AddToggle(const char* name, int* val, const datCallback &cb = NullCB, const char* description = nullptr);
    void AddToggle(const char* name, int* val, int mask, const datCallback &cb = NullCB, const char* description = nullptr);
    void AddToggle(const char* name, u16* val, u16 bitMask, const datCallback &cb = NullCB, const char* description = nullptr);
    void AddToggle(const char* name, u8* val, u16 bitMask, const datCallback &cb = NullCB, const char* description = nullptr);
    void AddToggle(const char* name, unsigned int* val, unsigned int mask, const datCallback &cb = NullCB, const char* description = nullptr);

    void AddText(const char* name, const char* val);
    void AddText(const char* name, void* val);
    void AddText(const char* name, char* buffer, int maxLen, bool flag = false, const datCallback &cb = NullCB);
    void AddText(const char* name, char* buffer, int maxLen, const datCallback &cb) { AddText(name, buffer, maxLen, false, cb); }

    void AddColor(const char* name, void* val, float step = 0.01f, const datCallback &cb = NullCB);

    class bkCombo* AddCombo(const char* name, int* val, int count, const char** list, int defaultIndex = 0, const datCallback &cb = NullCB, const char* description = nullptr);
    class bkCombo* AddCombo(const char* name, int* val, int count, const char** list, const datCallback &cb) { return AddCombo(name, val, count, list, 0, cb); }
    class bkCombo* AddCombo(const char* name, u8* val, int count, const char** list, int defaultIndex = 0, const datCallback &cb = NullCB, const char* description = nullptr);
    class bkCombo* AddCombo(const char* name, u8* val, int count, const char** list, const datCallback &cb) { return AddCombo(name, val, count, list, 0, cb); }
    class bkCombo* AddCombo(const char* name, s8* val, int count, const char** list, int defaultIndex = 0, const datCallback &cb = NullCB, const char* description = nullptr);
    class bkCombo* AddCombo(const char* name, s16* val, int count, const char** list, int defaultIndex = 0, const datCallback &cb = NullCB, const char* description = nullptr);
    class bkCombo* AddCombo(const char* name, u16* val, int count, const char** list, int defaultIndex = 0, const datCallback &cb = NullCB, const char* description = nullptr);
    void AddColor(const char* name, class Vector3* val, const datCallback &cb = NullCB) { AddColor(name, (void*)val, 0.01f, cb); }
    void AddColor(const char* name, class Vector4* val, const datCallback &cb = NullCB) { AddColor(name, (void*)val, 0.01f, cb); }

    class bkGroup* PushGroup(const char* name, bool collapsed = false);
    void PopGroup();
    
    void AddButton(const char* name, const datCallback &callback = NullCB, const char *description = nullptr);
    void AddTitle(const char* title);
    
    void Truncate(int val = 0) { Quitf("bkBank::Truncate - not implemented"); }

    void Resize(int w = 0, int h = 0);
    void Hide();
    void Show();
    void ToggleVisibility();
    bool IsShown() const;
    int GetNumWidgets() const;
    void GetState(bool &shown, int &x, int &y, int &w, int &h) const;
    void SetState(bool shown, int x, int y, int w, int h);
    void Raise(bool val = true);
    bool IsUncloseable() const { return m_Uncloseable; }
    void SetUncloseable(bool val = true);

    int GetWidth() const { return m_W; }
    int GetMaxHeight() const;
    int GetScrollY() const { return m_ScrollY; }
    int GetContentHeight() const { return m_ContentHeight; }

    void UpdateScrollRange();
    void ScrollTo(int newY);
    void HandleVScroll(unsigned short scrollCode, int pos);
    void HandleMouseWheel(short delta);

    const char* GetName() const { return m_Name.c_str(); }
    HWND GetHWND() const { return m_HWND; }

    void SimulateSliderChange(bkSlider* slider, float value);
    void SimulateToggleClick(const char* name);
    void SimulateButtonClick(const char* name);

private:
    datCallback m_PopulateCB;
    bool m_HasPopulateCB = false;
    bool m_Populated = true;

    void RegisterWidget(bkWidget* widget);
    void RebuildControls();

    std::string m_Name;
    HWND m_HWND;
    atArray<bkWidget*> m_Widgets;
    atArray<std::string> m_GroupStack;
    bool m_Shown;
    bool m_Uncloseable;
    int m_X, m_Y, m_W, m_H;
    int m_NextYOffset;
    int m_ScrollY;
    int m_ContentHeight;
};

#endif // __BANK

#endif // BANK_BANK_H
