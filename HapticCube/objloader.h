#ifndef OBJLOADER_H
#define OBJLOADER_H
#if defined(WIN32) || defined(linux)
#include <GL/glut.h>
#elif defined(__APPLE__)
#include <GLUT/glut.h>
#endif

#include <map>
#include <set>
#include <vector>
#include <glm/glm.hpp>
using namespace glm;
using namespace std;

struct Triangle{
    Triangle(int v0, int v1, int v2)
    {
        vert[0] = v0; vert[1] = v1; vert[2] = v2;
    }

    int vert[3];          // indices of vertices compose triangle
    vec3 normal;  // triangle normal
	vec3 center;
	int id;
};
	class OBJLoader {
	public:
		//! Constructor
		//!
		OBJLoader();

		//! Destructor
		//!
		~OBJLoader();

		bool load(const char *filename);

		std::vector<glm::vec3> const &getVertices() const;
		std::vector<glm::vec3> const &getNormals() const;
		std::vector<glm::vec3> const &getColors() const;
		std::vector<int> const &getVertexIndices() const;
		std::vector<int> const &getNormalIndices() const;
		std::vector<Triangle> const &getTriangles() const;
		std::vector<double> const &OBJLoader::getFriction() const;
		std::map<int, set<int>> net;

		void OBJLoader::Step(int n, int vertice, vec3 direction, float radius);
		void OBJLoader::deformPoint(int pointIndex, vec3 newPoint);
		float SmoothBell(float x);
		void computeNormals(std::vector<glm::vec3> const &vertices,
			std::vector<int> const &indices,
			std::vector<glm::vec3> &normals);
		
		void drawColorObj();
		void generate();
		void link(int a, int b);
		
		void unitize(std::vector<glm::vec3> &vertices);
		
	private:
		std::vector<glm::vec3> mVertices;
		std::vector<glm::vec3> mNormals;
		std::vector<glm::vec3> mColors;
		std::vector<double> mFriction;
		
		std::vector<int> vIndices;
		std::vector<int> nIndices;
		std::vector<Triangle> tris;
		
	};

#endif

