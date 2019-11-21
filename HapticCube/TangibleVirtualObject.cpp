#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <cmath>

#if defined(WIN32)
#include <windows.h>
#endif

#include <HL/hl.h>
#include <HDU/hduMath.h>
#include <HDU/hduMatrix.h>
#include <HDU/hduQuaternion.h>
#include <HDU/hduError.h>
#include <HLU/hlu.h>


#include "objloader.h"

using namespace std;

/* Haptic device and rendering context handles. */
static HHD ghHD = HD_INVALID_HANDLE;
static HHLRC ghHLRC = 0;

/* Shape id for shape we will render haptically. */


#define CURSOR_SIZE_PIXELS 20
static double gCursorScale;
static GLuint gCursorDisplayList = 0;

/* Struct representing one of the shapes in the scene that can be felt, touched and drawn. */
struct HapticObject
{
    HLuint shapeId;
    GLuint displayList;
    hduMatrix transform;
	float hap_stiffness;
    float hap_damping;
    float hap_static_friction;
    float hap_dynamic_friction;

	bool touched;

	OBJLoader loader;
};

vector<HapticObject> hapticObjects(0);
HapticObject pencilCursor; 

hduVector3Dd proxyInitialPosition;
int proxyTouchedPointIndex;

hduVector3Dd proxyPosition, devicePosition,newProxyPosition;

HLdouble proxyxform[16];

//Display list for model
GLuint objList;

float stiffnessCoefficient = 1.0;

HDSchedulerHandle gCallbackHandle = 0;
HDboolean bRenderForce = HD_FALSE;
HLboolean isAnchoredEditing = false;
HLboolean toggleCursor = false;
HLboolean isProxyConstrained = false;
static HDdouble gSpringStiffness = 0.1;
static HDdouble gMaxStiffness = 1.0;

bool buttonDown = false;

hduMatrix initialProxyTransformation;
hduMatrix currentProxyxform;

/* Function prototypes. */
void glutDisplay(void);
void glutReshape(int width, int height);
void glutIdle(void);   
void glutMenu(int); 
void keyboard(unsigned char key, int x, int y);
void exitHandler(void);

void initGL();
void initOBJModel();
void initHL();
void initScene();
void drawSceneHaptics();
void drawSceneGraphics();
void drawCursor();
void updateWorkspace();
void createHapticObject();
void updateDragObjTransform();
void drawConstrainedSpace();
int getIndexOfObject(int shapeID);

void HLCALLBACK buttonDownClientThreadCallback(HLenum event, HLuint object, HLenum thread, HLcache *cache, void *userdata);
void HLCALLBACK buttonUpClientThreadCallback(HLenum event, HLuint object, HLenum thread, HLcache *cache, void *userdata);

void HLCALLBACK hlMotionCB(HLenum event, HLuint object, HLenum thread, HLcache *cache, void * userdata);
void HLCALLBACK hlTouchCB(HLenum event, HLuint object, HLenum thread, HLcache *cache, void * userdata);
void HLCALLBACK hlUnTouchCB(HLenum event, HLuint object, HLenum thread, HLcache *cache, void *userdata);
HDCallbackCode HDCALLBACK anchoredSpringForceCallback(void *pUserData);

long int gCurrentDragObj = -1;
hduMatrix gStartProxyTransform;
hduMatrix gInitialObjTransform;

hduMatrix penCursorConfig;

long int gCurrentRotObj = -1;

vec3 touchedPoint;
int touchedPointIndex;

hduVector3Dd initialProxyPosition, initialDevicePosition, anchor, position, newModelPosition;
int findNearestPoint(vec3 pointPos, vector<vec3> vertices);
void drawPoint();

hduVector3Dd constrainedProxyPos;
hduVector3Dd minPoint;
hduVector3Dd maxPoint;

vector<vec3> mVerticeBuffer;

void generate();

int numSlices = 8;
const int maxNumSlices = 12;
set<int> immediateNeighbors[maxNumSlices];
//set<int> immediateNeighbors;
int rootTransformIndex;

void DisplayInfo(void);
void DrawBitmapString(GLfloat x, GLfloat y, void *font, char *format,...);
double getYVal(void);

/*******************************************************************************
 Initializes GLUT for displaying a simple haptic scene.
*******************************************************************************/
int main(int argc, char *argv[])
{
    glutInit(&argc, argv);
    
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);

    glutInitWindowSize(1000, 1000);
    glutCreateWindow("Tangible Virtual Object");

    // Set glut callback functions.
    glutDisplayFunc(glutDisplay);
    glutReshapeFunc(glutReshape);
    glutIdleFunc(glutIdle);
    glutKeyboardFunc(keyboard);
    glutCreateMenu(glutMenu);
    glutAddMenuEntry("Quit", 0);
    glutAttachMenu(GLUT_RIGHT_BUTTON);    
    
    // Provide a cleanup routine for handling application exit.
    atexit(exitHandler);

    initScene();

    glutMainLoop();

    return 0;
}

/*******************************************************************************
 GLUT callback for redrawing the view.
*******************************************************************************/
void glutDisplay()
{   
    drawSceneHaptics();
    drawSceneGraphics();
    glutSwapBuffers();
}

/*******************************************************************************
 GLUT callback for reshaping the window.  This is the main place where the 
 viewing and workspace transforms get initialized.
*******************************************************************************/
void glutReshape(int width, int height)
{
    static const double kPI = 3.1415926535897932384626433832795;
    static const double kFovY = 40;

    double nearDist, farDist, aspect;

    glViewport(0, 0, width, height);

    // Compute the viewing parameters based on a fixed fov and viewing
    // a canonical box centered at the origin.

    nearDist = 1.0 / tan((kFovY / 2.0) * kPI / 180.0);
    farDist = nearDist + 10.0;
    aspect = (double) width / height;
   
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(kFovY, aspect, nearDist, farDist);

    // Place the camera down the Z axis looking at the origin.
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();            
    gluLookAt(0, 5, nearDist + 3.0,
              0, 0, 0,
              0, 1, 0);
    
    updateWorkspace();
}

/*******************************************************************************
 GLUT callback for idle state.  Use this as an opportunity to request a redraw.
 Checks for HLAPI errors that have occurred since the last idle check.
*******************************************************************************/
void glutIdle()
{
    HLerror error;

    while (HL_ERROR(error = hlGetError()))
    {
        fprintf(stderr, "HL Error: %s\n", error.errorCode);
        
        if (error.errorCode == HL_DEVICE_ERROR)
        {
            hduPrintError(stderr, &error.errorInfo,
                "Error during haptic rendering\n");
        }
    }
    
    glutPostRedisplay();
}

/******************************************************************************
 Popup menu handler.
******************************************************************************/
void glutMenu(int ID)
{
    switch(ID) {
        case 0:
            exit(0);
            break;
    }
}
/******************************************************************************/
void keyboard(unsigned char key, int x, int y) {
	switch (key) {
	
	case 'a':
	case 'A':
		isAnchoredEditing = !isAnchoredEditing;
		if(isAnchoredEditing && (gCurrentDragObj !=-1)){
			
			initialProxyPosition = proxyPosition;
			initialDevicePosition = devicePosition;
            anchor = position;

			int hapticObjectIndex = getIndexOfObject(gCurrentDragObj);
			mVerticeBuffer = hapticObjects[hapticObjectIndex].loader.getVertices();

			hduMatrix mat = (hapticObjects[hapticObjectIndex].transform).getInverse();
			mat.multVecMatrix(proxyPosition, newModelPosition);
			vec3 pos(newModelPosition[0], newModelPosition[1], newModelPosition[2]);
			rootTransformIndex = findNearestPoint(pos, mVerticeBuffer);

			set<int> traversed;
			for(int i = 0; i < maxNumSlices; i++){
				immediateNeighbors[i].clear();
			}
	
			immediateNeighbors[0].insert(rootTransformIndex);
			traversed.insert(rootTransformIndex);

			for(int n = 1; n < maxNumSlices; n++){
				for(set<int>::iterator parentIndex = immediateNeighbors[n-1].begin(); 
					parentIndex != immediateNeighbors[n-1].end(); 
					parentIndex++){
					for(set<int>::iterator curIndex = hapticObjects[hapticObjectIndex].loader.net[*parentIndex].begin(); 
						curIndex != hapticObjects[hapticObjectIndex].loader.net[*parentIndex].end(); 
						curIndex++){
							if(traversed.find(*curIndex) == traversed.end()){
								immediateNeighbors[n].insert(*curIndex);
								traversed.insert(*curIndex);
							}
					}
				}
			}
			bRenderForce = HD_TRUE;
		}else{
			bRenderForce = HD_FALSE;
		}

		break;
	case '+':
		if(numSlices < maxNumSlices)
			numSlices++;
		break;
	case '-':
		if(numSlices > 1)
			numSlices--;
		break;
	case 't':
	case 'T':
		toggleCursor = !toggleCursor;
		break;
	case 'e':
	case 'E':
		isProxyConstrained = !isProxyConstrained;	
		if (isProxyConstrained)
		{
			constrainedProxyPos= proxyPosition;
			minPoint[0] = constrainedProxyPos[0] -0.25;
			minPoint[1] = constrainedProxyPos[1] -0.25;
			minPoint[2] = constrainedProxyPos[2] -0.25;
			maxPoint[0] = constrainedProxyPos[0] + 0.25;
			maxPoint[1] = constrainedProxyPos[1] + 0.25;
			maxPoint[2] = constrainedProxyPos[2] + 0.25;
		}
	}
}
/*******************************************************************************
 Initializes the scene.  Handles initializing both OpenGL and HL.
*******************************************************************************/
void initScene(){
	initOBJModel();
    initGL();
    initHL();
	createHapticObject();
}
/*******************************************************************************/
void initOBJModel(){
	hapticObjects.push_back(HapticObject());
	hapticObjects.push_back(HapticObject());

	bool loadfile = hapticObjects[0].loader.load("Plate.obj");
	hapticObjects[0].touched = false;

	loadfile = hapticObjects[1].loader.load("Bowl.obj");
	hapticObjects[1].touched = false;

	hapticObjects[0].transform = hduMatrix::createScale(1.3,1.3,1.3);
	hapticObjects[1].transform = hduMatrix::createTranslation(0,0.5,0);

	bool loadPencil = pencilCursor.loader.load("pencil.obj");
}
/*******************************************************************************
 Sets up general OpenGL rendering properties: lights, depth buffering, etc.
*******************************************************************************/
void initGL()
{
    static const GLfloat light_model_ambient[] = {0.3f, 0.3f, 0.3f, 1.0f};
    static const GLfloat light0_diffuse[] = {0.9f, 0.9f, 0.9f, 0.9f};   
    static const GLfloat light0_direction[] = {0.0f, -0.4f, 1.0f, 0.0f};    
    
    // Enable depth buffering for hidden surface removal.
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_DEPTH_TEST);

	glEnable(GL_COLOR_MATERIAL);
    
    // Cull back faces.
    glCullFace(GL_BACK);
    //glEnable(GL_CULL_FACE);
    
    // Setup other misc features.
    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);
    
    // Setup lighting model.
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_FALSE);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);    
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, light_model_ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light0_diffuse);
    glLightfv(GL_LIGHT0, GL_POSITION, light0_direction);
    glEnable(GL_LIGHT0);   
}

/*******************************************************************************
 Initialize the HDAPI.  This involves initing a device configuration, enabling
 forces, and scheduling a haptic thread callback for servicing the device.
*******************************************************************************/
void initHL()
{
    HDErrorInfo error;

	/* Start the haptic rendering loop. */

    ghHD = hdInitDevice(HD_DEFAULT_DEVICE);
    if (HD_DEVICE_ERROR(error = hdGetError()))
    {
        hduPrintError(stderr, &error, "Failed to initialize haptic device");
        fprintf(stderr, "Press any key to exit");
        getchar();
        exit(-1);
    }
	
	gCallbackHandle = hdScheduleAsynchronous(anchoredSpringForceCallback, 0, HD_DEFAULT_SCHEDULER_PRIORITY);
	hdEnable(HD_FORCE_OUTPUT);
    
	ghHLRC = hlCreateContext(ghHD);
    hlMakeCurrent(ghHLRC);

	hlEnable(HL_HAPTIC_CAMERA_VIEW);

}

/*******************************************************************************
 This handler is called when the application is exiting.  Deallocates any state 
 and cleans up.
*******************************************************************************/
void exitHandler()
{
    // Deallocate the sphere shape id we reserved in initHL.
	for(int i = 0; i < hapticObjects.size(); i++){
		hlDeleteShapes(hapticObjects[i].shapeId, 1);
	}

    // Free up the haptic rendering context.
    hlMakeCurrent(NULL);
    if (ghHLRC != NULL)
    {
        hlDeleteContext(ghHLRC);
    }

    hdUnschedule(gCallbackHandle);
    // Free up the haptic device.
    if (ghHD != HD_INVALID_HANDLE)
    {
        hdDisableDevice(ghHD);
    }
}

/*******************************************************************************
 Use the current OpenGL viewing transforms to initialize a transform for the
 haptic device workspace so that it's properly mapped to world coordinates.
*******************************************************************************/
void updateWorkspace()
{
    GLdouble modelview[16];
    GLdouble projection[16];
    GLint viewport[4];

    glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
    glGetDoublev(GL_PROJECTION_MATRIX, projection);
    glGetIntegerv(GL_VIEWPORT, viewport);

    hlMatrixMode(HL_TOUCHWORKSPACE);
    hlLoadIdentity();
    
    // Fit haptic workspace to view volume.
	if (!isProxyConstrained) hluFitWorkspace(projection);
	else hluFitWorkspaceBox( modelview, minPoint, maxPoint);

	//hluFitWorkspace(projection);
	
    // Compute cursor scale.
    gCursorScale = hluScreenToModelScale(modelview, projection, viewport);
    gCursorScale *= CURSOR_SIZE_PIXELS;
}

/*******************************************************************************/
void createHapticObject(){
	for(int i = 0; i < hapticObjects.size(); i++){
		hapticObjects[i].hap_stiffness = 0.8;
		hapticObjects[i].hap_damping = 0.0;
		hapticObjects[i].hap_static_friction = 0.5;
		hapticObjects[i].hap_dynamic_friction = 0.0;
    
		hapticObjects[i].shapeId = hlGenShapes(1);
		hapticObjects[i].displayList = glGenLists(1);
		hlAddEventCallback(HL_EVENT_1BUTTONDOWN, hapticObjects[i].shapeId, HL_CLIENT_THREAD, buttonDownClientThreadCallback, 0); 
		hlAddEventCallback(HL_EVENT_MOTION,  hapticObjects[i].shapeId, HL_COLLISION_THREAD, hlMotionCB, 0); 
		hlAddEventCallback(HL_EVENT_TOUCH, hapticObjects[i].shapeId, HL_COLLISION_THREAD, hlTouchCB, 0); 
		hlAddEventCallback(HL_EVENT_UNTOUCH, hapticObjects[i].shapeId, HL_COLLISION_THREAD, hlUnTouchCB, 0);
	}
	
	hlAddEventCallback(HL_EVENT_1BUTTONUP, HL_OBJECT_ANY, HL_CLIENT_THREAD, buttonUpClientThreadCallback, 0);
}
/*******************************************************************************
 The main routine for displaying the scene.  Gets the latest snapshot of state
 from the haptic thread and uses it to display a 3D cursor.
*******************************************************************************/
void drawSceneGraphics()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);           

    // Draw 3D cursor at haptic device position.
    drawCursor();
		
	//touchedPoint = hapticObjects[touchedIndex].loader.getVertices()[nearest];
	
	int closest = -1;

	for(int i = 0; i < hapticObjects.size(); i++){
		glPushMatrix();
		glMultMatrixd(hapticObjects[i].transform);
		
		hapticObjects[i].loader.drawColorObj();
		
		vec3 pos(proxyPosition[0], proxyPosition[1], proxyPosition[2]);
		int closest = findNearestPoint(pos, hapticObjects[i].loader.getVertices());

		//if (nearest > closest) {
		//	nearest = closest;
		//}

		glPopMatrix();
	}

	DisplayInfo();

	
}

/*******************************************************************************
 The main routine for rendering scene haptics.
*******************************************************************************/
void drawSceneHaptics()
{    
    // Start haptic frame.  (Must do this before rendering any haptic shapes.)
    hlBeginFrame();
	hlCheckEvents();

	updateWorkspace();

	HLboolean buttDown;
    hlGetBooleanv(HL_BUTTON1_STATE, &buttDown);
	if (buttDown){
	// "Drag" the current drag object, if one is current.
		if (gCurrentDragObj != -1){
			updateDragObjTransform();
		}
	}

	hlTouchModel(HL_CONTACT);
	hlTouchableFace(HL_FRONT);
	// Position and orient the object.
	glPushMatrix();
	// Set material properties for the shapes to be drawn.
	if (gCurrentDragObj == -1){ 
		for(int i = 0; i < hapticObjects.size(); i++){
			hlBeginShape(HL_SHAPE_FEEDBACK_BUFFER,hapticObjects[i].shapeId );
			hlMaterialf(HL_FRONT, HL_STIFFNESS, hapticObjects[i].hap_stiffness);
			hlMaterialf(HL_FRONT, HL_DAMPING, hapticObjects[i].hap_damping);
			if(hapticObjects[i].touched){
				hlMaterialf(HL_FRONT, HL_STATIC_FRICTION, hapticObjects[i].loader.getFriction()[touchedPointIndex]);
			}
			hlMaterialf(HL_FRONT, HL_DYNAMIC_FRICTION, hapticObjects[i].hap_dynamic_friction);

			// Start a new haptic shape.  Use the feedback buffer to capture OpenGL geometry for haptic rendering.
  
			glMultMatrixd(hapticObjects[i].transform);

			// Use OpenGL commands to create geometry.
			hapticObjects[i].loader.drawColorObj();
			// End the shape.
			hlEndShape();
		}
	}
	glPopMatrix();
    // End the haptic frame.
    hlEndFrame();
}


/*******************************************************************************
 Draws a 3D cursor for the haptic device using the current local transform,
 the workspace to world transform and the screen coordinate scale.
*******************************************************************************/
void drawCursor()
{
    static const double kCursorRadius = 0.5;
    static const double kCursorHeight = 1.5;
    static const int kCursorTess = 15;
   
	hlGetDoublev(HL_DEVICE_POSITION, devicePosition);
	hlGetDoublev(HL_PROXY_POSITION, proxyPosition);
	hlGetDoublev(HL_PROXY_TRANSFORM, proxyxform);
	
	hduVector3Dd devDifference = proxyPosition - proxyInitialPosition;
	if(bRenderForce){
		proxyxform[12] = devDifference[0];
		proxyxform[13] = devDifference[1];
		proxyxform[14] = devDifference[2];
	}

    GLUquadricObj *qobj = 0;

    glPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT | GL_LIGHTING_BIT);
    glPushMatrix();

    if (!gCursorDisplayList)
    {
		gCursorDisplayList = glGenLists(1);
		glNewList(gCursorDisplayList, GL_COMPILE);
		qobj = gluNewQuadric();
               
		gluCylinder(qobj, 0.0, kCursorRadius, kCursorHeight, kCursorTess, kCursorTess);
		glTranslated(0.0, 0.0, kCursorHeight);
		gluCylinder(qobj, kCursorRadius, 0.0, kCursorHeight / 5.0, kCursorTess, kCursorTess);
		gluDeleteQuadric(qobj);
		glEndList();

		hduMatrix rotation = hduMatrix::createRotationAroundX(3.1415926 / 2.0);
		hduMatrix translation = hduMatrix::createTranslation(0,0.4,0);
		hduMatrix scale = hduMatrix::createScale(10,10,10);
		penCursorConfig = scale * translation * rotation;
    }
    
    // Get the proxy transform in world coordinates.

	if( bRenderForce){
		proxyxform[12] = newProxyPosition[0];
		proxyxform[13] = newProxyPosition[1];
		proxyxform[14] = newProxyPosition[2];
	}

	
	if(toggleCursor){
		glMultMatrixd(proxyxform);
	}else{
		hduMatrix proxyxform;
		hlGetDoublev(HL_PROXY_TRANSFORM, proxyxform);
		
		glMultMatrixd(penCursorConfig * proxyxform);
	}

	glEnable(GL_COLOR_MATERIAL);
    glColor3f(0.0, 0.5, 1.0);
	// Apply the local cursor scale factor.
	glScaled(gCursorScale, gCursorScale, gCursorScale);
	if(toggleCursor){
		glCallList(gCursorDisplayList);
	}else{
		pencilCursor.loader.drawColorObj();
	}
    glPopMatrix(); 
    glPopAttrib();

	if(isProxyConstrained){
		drawConstrainedSpace();
	}
}

/******************************************************************************/

void HLCALLBACK buttonDownClientThreadCallback(HLenum event, HLuint object, HLenum thread, HLcache *cache, void *userdata){
	gCurrentDragObj = object;
	hlGetDoublev(HL_PROXY_TRANSFORM, gStartProxyTransform);
	for(int i = 0; i < hapticObjects.size(); i++){
		if(object == hapticObjects[i].shapeId){
			gInitialObjTransform =  hapticObjects[i].transform;
			hapticObjects[i].touched = true;
		}else{
			hapticObjects[i].touched = false;
		}
	}
	buttonDown = true;
}

void HLCALLBACK buttonUpClientThreadCallback(HLenum event, HLuint object, HLenum thread, HLcache *cache, void *userdata){
	if (gCurrentDragObj != -1)
		gCurrentDragObj = -1;
	buttonDown = false;
	isAnchoredEditing = false;
	bRenderForce = false;
}


void HLCALLBACK hlMotionCB(HLenum event, HLuint object, HLenum thread, HLcache *cache, void * userdata){
	if(gCurrentDragObj != -1){
		int index = getIndexOfObject(gCurrentDragObj);
		if(index != -1){
			hduMatrix mat = (hapticObjects[index].transform).getInverse();

			hduVector3Dd transformedProxyPos;
			mat.multVecMatrix(proxyPosition, transformedProxyPos);
			vec3 pos(transformedProxyPos[0], transformedProxyPos[1], transformedProxyPos[2]);

			int nearest = findNearestPoint(pos, hapticObjects[index].loader.getVertices());
			touchedPoint = hapticObjects[index].loader.getVertices()[nearest];
			touchedPointIndex = nearest;
			hapticObjects[index].hap_static_friction = hapticObjects[index].loader.getFriction()[nearest];
			//printf("Friction:%f\n",hapticObject.surfaceFriction[nearest]);
		}
	}
}


void HLCALLBACK hlTouchCB(HLenum event, HLuint object, HLenum thread, HLcache *cache, void * userdata){
	int hapticIndex = getIndexOfObject(object);
	if(hapticIndex != -1){
		hapticObjects[hapticIndex].touched = true;
	}
}

void HLCALLBACK hlUnTouchCB(HLenum event, HLuint object, HLenum thread, HLcache *cache, void *userdata){
	int hapticIndex = getIndexOfObject(object);
	if(hapticIndex != -1){
		hapticObjects[hapticIndex].touched = false;
	}
}

double getYVal(){
	switch(numSlices){
	case 9:
		return 3.5;
	case 8:
		return 4;
	case 7:
		return 5;
	case 6:
		return 7;
	case 5:
		return 9;
	case 4:
		return 12;
	case 3:
		return 20;
	case 2:
		return 40;
	default:
		return 3;
	}
}

void DrawBitmapString(GLfloat x, GLfloat y, void *font, char *format,...)
{
    int len, i;
    va_list args;
    char string[256];

// special C stuff to interpret a dynamic set of arguments specified by "..."
    va_start(args, format);
    vsprintf(string, format, args);
    va_end(args);

    glRasterPos2f(x, y);
    len = (int) strlen(string);

    for (i = 0; i < len; i++)
    {
        glutBitmapCharacter(font, string[i]);
    }
}

void DisplayInfo(){
	glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glPushMatrix();
    glLoadIdentity();

    int gwidth, gheight;
    gwidth = glutGet(GLUT_WINDOW_WIDTH);
    gheight = glutGet(GLUT_WINDOW_HEIGHT);

// switch to 2d orthographic mMode for drawing text
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();

    gluOrtho2D(0, gwidth, gheight, 0);
    glMatrixMode(GL_MODELVIEW);

    glColor3f(1.0, 1.0, 1.0);

    int textRowDown = 0;                                    // lines of text already drawn downwards from the top

    int textRowUp = 0;                                      // lines of text already drawn upwards from the bottom

    DrawBitmapString(0 , 20 , GLUT_BITMAP_HELVETICA_18, "INSTRUCTIONS: ");
    DrawBitmapString(0 , 40 , GLUT_BITMAP_HELVETICA_18, "Use '+' and '-' keys to increase or decrease the deformation radius.");
	DrawBitmapString(0 , 60 , GLUT_BITMAP_HELVETICA_18, "Current Radius: %d", numSlices);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

// turn depth and lighting back on for 3D rendering
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
}

HDCallbackCode HDCALLBACK anchoredSpringForceCallback(void *pUserData){
	hduVector3Dd force(0, 0, 0);
	HDErrorInfo error;
	hdBeginFrame(hdGetCurrentDevice());
	hdGetDoublev(HD_CURRENT_POSITION, position); 

	if (bRenderForce && gCurrentDragObj != -1){
		HapticObject* myObj = &hapticObjects[getIndexOfObject(gCurrentDragObj)];

		hduVector3Dd devDifference = devicePosition - initialDevicePosition;

		newProxyPosition = initialProxyPosition + devDifference;
		force = (anchor-position)*gSpringStiffness;
		hdSetDoublev(HD_CURRENT_FORCE, force);
		hduMatrix mat = ((*myObj).transform).getInverse();
		mat.multVecMatrix(newProxyPosition, newModelPosition);
		
		vec3 myNormal;

		myNormal.x = newModelPosition[0] - mVerticeBuffer[rootTransformIndex].x;
		myNormal.y = newModelPosition[1] - mVerticeBuffer[rootTransformIndex].y;
		myNormal.z = newModelPosition[2] - mVerticeBuffer[rootTransformIndex].z;

		for(int n = 1; n <= numSlices; n++){
			for(set<int>::iterator cur_b = immediateNeighbors[n-1].begin(); cur_b != immediateNeighbors[n-1].end(); cur_b++){
				double yVal = getYVal();
				double scalar = 1/(1+ pow(yVal,(n-double(numSlices)/2.0)));
				
				mVerticeBuffer[*cur_b].x += myNormal.x*scalar;
				mVerticeBuffer[*cur_b].y += myNormal.y*scalar;
				mVerticeBuffer[*cur_b].z += myNormal.z*scalar;
				(*myObj).loader.deformPoint(*cur_b, mVerticeBuffer[*cur_b]);
			}
		}
	}

	hdEndFrame(hdGetCurrentDevice());
	if (HD_DEVICE_ERROR(error = hdGetError())) {
		if (hduIsForceError(&error)) {
			bRenderForce = HD_FALSE;
		}
		else if (
			hduIsSchedulerError(&error)) {
				return HD_CALLBACK_DONE;
		}
	}
	
	return HD_CALLBACK_CONTINUE;
	
}

void updateDragObjTransform(){

	int hapticIndex = getIndexOfObject(gCurrentDragObj);
	if(hapticIndex != -1 && !bRenderForce){
		hduMatrix proxyxform;
		hlGetDoublev(HL_PROXY_TRANSFORM, proxyxform);

		// Translation part

		hduVector3Dd proxyPos(proxyxform[3][0], proxyxform[3][1], proxyxform[3][2] );
		hduVector3Dd gstartDragProxyPos(gStartProxyTransform[3][0], gStartProxyTransform[3][1], gStartProxyTransform[3][2]);
		hduVector3Dd dragDeltaTransl = proxyPos - gstartDragProxyPos;
		hduMatrix deltaMat = hduMatrix::createTranslation(dragDeltaTransl);

		hduMatrix initialRotationMatrix = gStartProxyTransform;
		initialRotationMatrix[3][0] = 0.0;
		initialRotationMatrix[3][1] = 0.0;
		initialRotationMatrix[3][2] = 0.0;
		hduMatrix currentRotationMatrix = proxyxform;
		currentRotationMatrix[3][0] = 0.0;
		currentRotationMatrix[3][1] = 0.0;
		currentRotationMatrix[3][2] = 0.0;
		// Apply these deltas to the drag object transform.

		hduMatrix deltaRotationMatrix = initialRotationMatrix.getInverse()*currentRotationMatrix;

		hduMatrix toCenter = hduMatrix::createTranslation(-proxyPos);
		hduMatrix deltaRotation;
		hduMatrix fromCenter = hduMatrix::createTranslation(proxyPos);

		hduMatrix overallDeltaRotation = toCenter*deltaRotationMatrix*fromCenter;

		hapticObjects[hapticIndex].transform = (gInitialObjTransform * deltaMat) * overallDeltaRotation;
	}
}

int findNearestPoint(vec3 pointPos, vector<vec3> vertices){
	int closestIndex;
	double minDist = -1;

	for(int i = 0; i < vertices.size(); i++){
		double xDist = vertices[i].x - pointPos.x;
		double yDist = vertices[i].y - pointPos.y;
		double zDist = vertices[i].z - pointPos.z;

		double distance = sqrt(pow(xDist,2) + pow(yDist,2) + pow(zDist,2));
		if(minDist == -1 || minDist > distance){
			minDist = distance;
			closestIndex = i;
		}
		
	}
	return closestIndex;
}


void drawPoint(){
	glPointSize(10.0f);
	glBegin(GL_POINTS); 
	
	glColor3f(1.0,1.0,0.0);

	glVertex3f(touchedPoint[0], touchedPoint[1], touchedPoint[2]);
	
	glEnd();
}

int getIndexOfObject(int shapeID){
	for(int i = 0; i < hapticObjects.size(); i++){
		if(hapticObjects[i].shapeId == shapeID){
			return i;
		}
	}
	return -1;
}

void drawConstrainedSpace(){
	
	vec3 Point0(minPoint[0], minPoint[1], minPoint[2]);
	vec3 Point1(minPoint[0], minPoint[1], maxPoint[2]);
	vec3 Point2(maxPoint[0], minPoint[1], maxPoint[2]);
	vec3 Point3(maxPoint[0], minPoint[1], minPoint[2]);
	vec3 Point4(maxPoint[0], maxPoint[1], minPoint[2]);
	vec3 Point5(maxPoint[0], maxPoint[1], maxPoint[2]);
	vec3 Point6(minPoint[0], maxPoint[1], maxPoint[2]);
	vec3 Point7(minPoint[0], maxPoint[1], minPoint[2]);

	glPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT |GL_LIGHTING_BIT);
	glLineWidth(10.0f);
	glEnable(GL_COLOR_MATERIAL);
	glBegin(GL_LINES);// Draw Line for stationary pinch handle

	glColor3f(1.0,0.0,0.0);
	glVertex3f(Point0[0],Point0[1],Point0[2]);
	glVertex3f(Point1[0],Point1[1],Point1[2]);

	glColor3f(1.0,0.0,0.0);
	glVertex3f(Point1[0],Point1[1],Point1[2]);
	glVertex3f(Point2[0],Point2[1],Point2[2]);

	glColor3f(1.0,0.0,0.0);
	glVertex3f(Point2[0],Point2[1],Point2[2]);
	glVertex3f(Point3[0],Point3[1],Point3[2]);

	glColor3f(1.0,0.0,0.0);
	glVertex3f(Point3[0],Point3[1],Point3[2]);
	glVertex3f(Point0[0],Point0[1],Point0[2]);

	glColor3f(1.0,0.0,0.0);
	glVertex3f(Point0[0],Point0[1],Point0[2]);
	glVertex3f(Point7[0],Point7[1],Point7[2]);

	glColor3f(1.0,0.0,0.0);
	glVertex3f(Point1[0],Point1[1],Point1[2]);
	glVertex3f(Point6[0],Point6[1],Point6[2]);

	glColor3f(1.0,0.0,0.0);
	glVertex3f(Point2[0],Point2[1],Point2[2]);
	glVertex3f(Point5[0],Point5[1],Point5[2]);

	glColor3f(1.0,0.0,0.0);
	glVertex3f(Point3[0],Point3[1],Point3[2]);
	glVertex3f(Point4[0],Point4[1],Point4[2]);

	glColor3f(1.0,0.0,0.0);
	glVertex3f(Point6[0],Point6[1],Point6[2]);
	glVertex3f(Point7[0],Point7[1],Point7[2]);

	glColor3f(1.0,0.0,0.0);
	glVertex3f(Point5[0],Point5[1],Point5[2]);
	glVertex3f(Point6[0],Point6[1],Point6[2]);

	glColor3f(1.0,0.0,0.0);
	glVertex3f(Point4[0],Point4[1],Point4[2]);
	glVertex3f(Point5[0],Point5[1],Point5[2]);

	glColor3f(1.0,0.0,0.0);
	glVertex3f(Point7[0],Point7[1],Point7[2]);
	glVertex3f(Point4[0],Point4[1],Point4[2]);

	glEnd();
	glPopAttrib();
}

