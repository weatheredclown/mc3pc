#ifndef VECTOR_SITUATION_H
#define VECTOR_SITUATION_H

#include "vector/vector3.h"
#include "vector/matrix34.h"
#include "vector/quaternion.h"

class Situation {
public:
	Vector3 m_Position;
	Quaternion m_Orientation;

	Situation() {
		Zero();
	}

	void Zero() {
		m_Position.Zero();
		m_Orientation.Set(0.0f, 0.0f, 0.0f, 1.0f);
	}

	void FromMatrix34(const Matrix34 &matrix) {
		m_Position = matrix.d;
		m_Orientation.FromMatrix34(matrix);
	}

	void ToMatrix34(Matrix34 &matrix) const {
		m_Orientation.ToMatrix34(matrix);
		matrix.d = m_Position;
	}
};

#endif // VECTOR_SITUATION_H
