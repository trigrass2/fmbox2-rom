
#include "fmbox.h"

#define WIN_W (OLED_W*2)
#define WIN_H (OLED_W*2)

uint8_t oled_pix[OLED_W*OLED_H*3];

static void printtext(int x, int y, char *str) {
	//(x,y) is from the bottom left of the window
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, WIN_W, 0, WIN_H, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glPushAttrib(GL_DEPTH_TEST);
	glDisable(GL_DEPTH_TEST);
	glRasterPos2i(x, y);

	int i;
	for (i = 0; i < strlen(str); i++) {
		glutBitmapCharacter(GLUT_BITMAP_9_BY_15, str[i]);
	}

	glPopAttrib();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}

static void display() {
	//printf("[GL] Display\n");

	glClear(GL_COLOR_BUFFER_BIT);

	glColor3f(1, 1, 1);
	glBegin(GL_POLYGON);
	glVertex2f(-1, -1);  
	glVertex2f(-1, 1);		
	glVertex2f(1, 1);  
	glVertex2f(1, -1);  
	glEnd();

	float fact = 0.4;
	float wfact = 1.;
	float hfact = (float)OLED_H/OLED_W;

	glEnable(GL_TEXTURE_2D);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
	glTexImage2D(GL_TEXTURE_2D, 0, 3, OLED_W, OLED_H, 0, GL_RGB,
			GL_UNSIGNED_BYTE, oled_pix);

	glBegin(GL_POLYGON);
	glTexCoord2f(1.0, 0.0);
	glVertex2f(fact*wfact, fact*hfact);
	glTexCoord2f(0.0, 0.0);
	glVertex2f(-fact*wfact, fact*hfact);
	glTexCoord2f(0.0, 1.0);
	glVertex2f(-fact*wfact, -fact*hfact);
	glTexCoord2f(1.0, 1.0);
	glVertex2f(fact*wfact, -fact*hfact);
	glEnd();
	glDisable(GL_TEXTURE_2D);

	glColor3f(0, 0, 0);
	printtext(140, 100, "ASDF = up/down/left/right, C = cancel");
	printtext(140, 80, "L = love, N = next, T = trash");

	glutSwapBuffers();
}

static void wait_for_data() {
	glutPostRedisplay();
	sleep(1000/24);
}

void ui_glut_main(int argc, char **argv) {
	glutInit(&argc, argv);  
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
	glutInitWindowSize(WIN_W, WIN_H);
	glutCreateWindow("Fmbox");
	glutDisplayFunc(display);
	glutIdleFunc(wait_for_data);

	glutMainLoop();
}
