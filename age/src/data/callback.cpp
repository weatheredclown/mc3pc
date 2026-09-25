#include "data/callback.h"

datCallbackList g_datEndFrameList;

void datCallbackList::Add(const datCallback &cb)
{
	if (m_Count < kMax) m_Items[m_Count++] = new datCallback(cb);
}

void datCallbackList::Remove(const datCallback & /*cb*/)
{
}

void datCallbackList::Call(void *param) const
{
	for (int i = 0; i < m_Count; i++) if (m_Items[i]) m_Items[i]->Call(param);
}
