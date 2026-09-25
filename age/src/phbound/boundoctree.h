#ifndef PHBOUND_BOUNDOCTREE_H
#define PHBOUND_BOUNDOCTREE_H

#include "phbound/boundpolyhedron.h"

class phBoundOctree : public phBoundPolyhedron {
public:
	phBoundOctree() {}
	virtual ~phBoundOctree() {}

	virtual int GetType() const { return OCTREE; }
};

#endif // PHBOUND_BOUNDOCTREE_H
