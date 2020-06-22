#include <QApplication>
#include <QThread>
#include <QMutex>
#include <QMessageBox>

#include <QColor>
#include <QLabel>
#include <QtDebug>
#include <QString>
#include <QPushButton>

#include "LeptonThread.h"
#include "MyLabel.h"

void printUsage(char *cmd)
{
	char *cmdname = basename(cmd);
	printf("Usage: %s [OPTION]...\n"
				 " -h      display this help and exit\n"
				 " -cm x   select colormap\n"
				 "           1 : rainbow\n"
				 "           2 : grayscale\n"
				 "           3 : ironblack [default]\n"
				 " -tl x   select type of Lepton\n"
				 "           2 : Lepton 2.x\n"
				 "           3 : Lepton 3.x [default]\n"
				 "               [for your reference] Please use nice command\n"
				 "                 e.g. sudo nice -n 0 ./%s -tl 3\n"
				 " -ss x   SPI bus speed [MHz] (10 - 30)\n"
				 "           20 : 20MHz [default]\n"
				 " -min x  override minimum value for scaling (0 - 65535)\n"
				 "           [default] automatic scaling range adjustment\n"
				 "           e.g. -min 29500\n"
				 " -max x  override maximum value for scaling (0 - 65535)\n"
				 "           [default] automatic scaling range adjustment\n"
				 "           e.g. -max 31200\n"
				 " -mirror mirror mode\n"
				 " -auto   auto capture mode\n"
				 " -d x    log level (0-255)\n"
				 "",
				 cmdname, cmdname);
	return;
}

int main(int argc, char **argv)
{
	int typeColormap = 3; // colormap_ironblack
	int typeLepton = 3;		// Lepton 3.x
	int spiSpeed = 20;		// SPI bus speed 20MHz
	int rangeMin = -1;		//
	int rangeMax = -1;		//
	int loglevel = 0;
	bool mirror = false;
	bool auto_capture = false;

	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-h") == 0)
		{
			printUsage(argv[0]);
			exit(0);
		}
		else if (strcmp(argv[i], "-d") == 0)
		{
			int val = 3;
			if ((i + 1 != argc) && (strncmp(argv[i + 1], "-", 1) != 0))
			{
				val = std::atoi(argv[i + 1]);
				i++;
			}
			if (0 <= val)
			{
				loglevel = val & 0xFF;
			}
		}
		else if ((strcmp(argv[i], "-cm") == 0) && (i + 1 != argc))
		{
			int val = std::atoi(argv[i + 1]);
			if ((val == 1) || (val == 2))
			{
				typeColormap = val;
				i++;
			}
		}
		else if ((strcmp(argv[i], "-tl") == 0) && (i + 1 != argc))
		{
			int val = std::atoi(argv[i + 1]);
			if (val == 3)
			{
				typeLepton = val;
				i++;
			}
		}
		else if ((strcmp(argv[i], "-ss") == 0) && (i + 1 != argc))
		{
			int val = std::atoi(argv[i + 1]);
			if ((10 <= val) && (val <= 30))
			{
				spiSpeed = val;
				i++;
			}
		}
		else if ((strcmp(argv[i], "-min") == 0) && (i + 1 != argc))
		{
			int val = std::atoi(argv[i + 1]);
			if ((0 <= val) && (val <= 65535))
			{
				if (val < 3000)
				{
					// val = (val * 91.0) + 27700;
					val = (val + 177.77 + 450.0) / 0.0217;
				}
				rangeMin = val;
				i++;
			}
		}
		else if ((strcmp(argv[i], "-max") == 0) && (i + 1 != argc))
		{
			int val = std::atoi(argv[i + 1]);
			if ((0 <= val) && (val <= 65535))
			{
				if (val < 3000)
				{
					// val = (val * 91.0) + 27700;
					val = (val + 177.77 + 450.0) / 0.0217;
				}
				rangeMax = val;
				i++;
			}
		}
		else if (strcmp(argv[i], "-mirror") == 0)
		{
			mirror = true;
		}
		else if (strcmp(argv[i], "-auto") == 0)
		{
			auto_capture = true;
		}
	}

	//create the app
	QApplication a(argc, argv);

	int width = 160 * 3;
	int height = 120 * 3;

	QWidget *myWidget = new QWidget;
	myWidget->setGeometry(120, 80, width + 20, height + 60);

	//create an image placeholder for myLabel
	//fill the top left corner with red, just bcuz
	QImage myImage;
	myImage = QImage(width, height, QImage::Format_RGB888);
	QRgb red = qRgb(255, 0, 0);
	for (int i = 0; i < 80; i++)
	{
		for (int j = 0; j < 60; j++)
		{
			myImage.setPixel(i, j, red);
		}
	}

	//create a label, and set it's image to the placeholder
	MyLabel myLabel(myWidget);
	myLabel.setGeometry(10, 10, width, height);
	myLabel.setPixmap(QPixmap::fromImage(myImage));

	//create a Capture button
	QPushButton *button1 = new QPushButton("Capture", myWidget);
	button1->setGeometry((width + 20) / 2 - 50, height + 20, 100, 30);

	//create a Thermal label

	QLabel *label = new QLabel(myWidget);
	label->setText("0");
	label->setGeometry((width + 20) / 3 * 2, height + 20, 100, 30);

	//create a thread to gather SPI data
	//when the thread emits updateImage, the label should update its image accordingly
	LeptonThread *thread = new LeptonThread();
	thread->setMirror(mirror);
	thread->setAutoCapture(auto_capture);
	thread->setLogLevel(loglevel);
	thread->useColormap(typeColormap);
	thread->useLepton(typeLepton);
	thread->useSpiSpeedMhz(spiSpeed);
	thread->setAutomaticScalingRange();
	if (0 <= rangeMin)
		thread->useRangeMinValue(rangeMin);
	if (0 <= rangeMax)
		thread->useRangeMaxValue(rangeMax);

	//connect image
	QObject::connect(thread, SIGNAL(updateImage(QImage)), &myLabel, SLOT(setImage(QImage)));

	//connect string
	QObject::connect(thread, SIGNAL(updateText(QString)), label, SLOT(setText(QString)));

	//connect capture button to the thread's capture action
	QObject::connect(button1, SIGNAL(clicked()), thread, SLOT(capture()));
	thread->start();

	myWidget->show();

	return a.exec();
}
