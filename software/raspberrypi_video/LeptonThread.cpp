#include <iostream>

#include "LeptonThread.h"

#include "Palettes.h"
#include "SPI.h"
#include "Lepton_I2C.h"

#define PACKET_SIZE 164
#define PACKET_SIZE_UINT16 (PACKET_SIZE / 2)
#define PACKETS_PER_FRAME 60
#define FRAME_SIZE_UINT16 (PACKET_SIZE_UINT16 * PACKETS_PER_FRAME)
#define FPS 27;

LeptonThread::LeptonThread() : QThread()
{
	//
	loglevel = 0;

	//
	mirror = false;
	auto_capture = false;

	//
	typeColormap = 3; // 1:colormap_rainbow  /  2:colormap_grayscale  /  3:colormap_ironblack(default)
	selectedColormap = colormap_ironblack;
	selectedColormapSize = get_size_colormap_ironblack();

	//
	typeLepton = 3; // 2:Lepton 2.x  / 3:Lepton 3.x
	myImageWidth = 80;
	myImageHeight = 60;

	//
	spiSpeed = 20 * 1000 * 1000; // SPI bus speed 20MHz

	// min/max value for scaling
	autoRangeMin = true;
	autoRangeMax = true;
	rangeMin = 29500;
	rangeMax = 31200;
}

LeptonThread::~LeptonThread()
{
}

void LeptonThread::setLogLevel(uint16_t newLoglevel)
{
	loglevel = newLoglevel;
}

void LeptonThread::setMirror(bool val)
{
	mirror = val;
}

void LeptonThread::setAutoCapture(bool val)
{
	auto_capture = val;
}

void LeptonThread::useColormap(int newTypeColormap)
{
	switch (newTypeColormap)
	{
	case 1:
		typeColormap = 1;
		selectedColormap = colormap_rainbow;
		selectedColormapSize = get_size_colormap_rainbow();
		break;
	case 2:
		typeColormap = 2;
		selectedColormap = colormap_grayscale;
		selectedColormapSize = get_size_colormap_grayscale();
		break;
	default:
		typeColormap = 3;
		selectedColormap = colormap_ironblack;
		selectedColormapSize = get_size_colormap_ironblack();
		break;
	}
}

void LeptonThread::useLepton(int newTypeLepton)
{
	switch (newTypeLepton)
	{
	case 3:
		typeLepton = 3;
		myImageWidth = 160;
		myImageHeight = 120;
		break;
	default:
		typeLepton = 2;
		myImageWidth = 80;
		myImageHeight = 60;
	}
}

void LeptonThread::useSpiSpeedMhz(unsigned int newSpiSpeed)
{
	spiSpeed = newSpiSpeed * 1000 * 1000;
}

void LeptonThread::setAutomaticScalingRange()
{
	autoRangeMin = true;
	autoRangeMax = true;
}

void LeptonThread::useRangeMinValue(uint16_t newMinValue)
{
	autoRangeMin = false;
	rangeMin = newMinValue;
}

void LeptonThread::useRangeMaxValue(uint16_t newMaxValue)
{
	autoRangeMax = false;
	rangeMax = newMaxValue;
}

void LeptonThread::run()
{
	//create the initial image
	myImage = QImage(myImageWidth, myImageHeight, QImage::Format_RGB888);

	const int *colormap = selectedColormap;
	const int colormapSize = selectedColormapSize;
	uint16_t minValue = rangeMin;
	uint16_t maxValue = rangeMax;
	float diff = maxValue - minValue;
	float scale = 255 / diff;
	uint16_t n_wrong_segment = 0;
	uint16_t n_zero_value_drop_frame = 0;

	QRgb crosshair = qRgb(0, 0, 255);

	//open spi port
	SpiOpenPort(0, spiSpeed);

	while (true)
	{
		//read data packets from lepton over SPI
		int resets = 0;
		int segmentNumber = -1;
		for (int j = 0; j < PACKETS_PER_FRAME; j++)
		{
			//if it's a drop packet, reset j to 0, set to -1 so he'll be at 0 again loop
			read(spi_cs0_fd, result + sizeof(uint8_t) * PACKET_SIZE * j, sizeof(uint8_t) * PACKET_SIZE);
			int packetNumber = result[j * PACKET_SIZE + 1];
			if (packetNumber != j)
			{
				j = -1;
				resets += 1;
				usleep(1000);
				//Note: we've selected 750 resets as an arbitrary limit, since there should never be 750 "null" packets between two valid transmissions at the current poll rate
				//By polling faster, developers may easily exceed this count, and the down period between frames may then be flagged as a loss of sync
				if (resets == 750)
				{
					SpiClosePort(0);
					lepton_reboot();
					n_wrong_segment = 0;
					n_zero_value_drop_frame = 0;
					usleep(750000);
					SpiOpenPort(0, spiSpeed);
				}
				continue;
			}
			if ((typeLepton == 3) && (packetNumber == 20))
			{
				segmentNumber = (result[j * PACKET_SIZE] >> 4) & 0x0f;
				if ((segmentNumber < 1) || (4 < segmentNumber))
				{
					log_message(10, "[ERROR] Wrong segment number " + std::to_string(segmentNumber));
					break;
				}
			}
		}
		if (resets >= 30)
		{
			log_message(3, "done reading, resets: " + std::to_string(resets));
		}

		//
		int iSegmentStart = 1;
		int iSegmentStop;
		if (typeLepton == 3)
		{
			if ((segmentNumber < 1) || (4 < segmentNumber))
			{
				n_wrong_segment++;
				if ((n_wrong_segment % 12) == 0)
				{
					log_message(5, "[WARNING] Got wrong segment number continuously " + std::to_string(n_wrong_segment) + " times");
				}
				continue;
			}
			if (n_wrong_segment != 0)
			{
				log_message(8, "[WARNING] Got wrong segment number continuously " + std::to_string(n_wrong_segment) + " times [RECOVERED] : " + std::to_string(segmentNumber));
				n_wrong_segment = 0;
			}

			//
			memcpy(shelf[segmentNumber - 1], result, sizeof(uint8_t) * PACKET_SIZE * PACKETS_PER_FRAME);
			if (segmentNumber != 4)
			{
				continue;
			}
			iSegmentStop = 4;
		}
		else
		{
			memcpy(shelf[0], result, sizeof(uint8_t) * PACKET_SIZE * PACKETS_PER_FRAME);
			iSegmentStop = 1;
		}

		uint16_t minTemp = 65535;
		uint16_t maxTemp = 0;
		bool over_heat = false;
		int max_row = 0, max_column = 0;

		for (int iSegment = iSegmentStart; iSegment <= iSegmentStop; iSegment++)
		{
			for (int i = 0; i < FRAME_SIZE_UINT16; i++)
			{
				//skip the first 2 uint16_t's of every packet, they're 4 header bytes
				if (i % PACKET_SIZE_UINT16 < 2)
				{
					continue;
				}

				//flip the MSB and LSB at the last second
				uint16_t value = (shelf[iSegment - 1][i * 2] << 8) + shelf[iSegment - 1][i * 2 + 1];
				if (value == 0)
				{
					// Why this value is 0?
					continue;
				}
				if (value > maxTemp)
				{
					maxTemp = value;
				}
				if (value < minTemp)
				{
					minTemp = value;
				}
			}
		}

		if ((autoRangeMin == true) || (autoRangeMax == true))
		{
			if (autoRangeMin == true)
			{
				minValue = minTemp;
			}
			if (autoRangeMax == true)
			{
				maxValue = maxTemp;
			}
			diff = maxValue - minValue;
			scale = 255 / diff;
		}

		if (maxTemp >= maxValue)
		{
			over_heat = true;
		}

		printf("%d (%.1f) : %d (%.1f)\n", minTemp, (minTemp - 27700) / 90.0, maxTemp, (maxTemp - 27700) / 90.0);

		int row, column;
		uint16_t value;
		uint16_t valueFrameBuffer;
		QRgb color;
		for (int iSegment = iSegmentStart; iSegment <= iSegmentStop; iSegment++)
		{
			int ofsRow = 30 * (iSegment - 1);
			for (int i = 0; i < FRAME_SIZE_UINT16; i++)
			{
				//skip the first 2 uint16_t's of every packet, they're 4 header bytes
				if (i % PACKET_SIZE_UINT16 < 2)
				{
					continue;
				}

				//flip the MSB and LSB at the last second
				valueFrameBuffer = (shelf[iSegment - 1][i * 2] << 8) + shelf[iSegment - 1][i * 2 + 1];
				if (valueFrameBuffer == 0)
				{
					// Why this value is 0?
					n_zero_value_drop_frame++;
					if ((n_zero_value_drop_frame % 12) == 0)
					{
						log_message(5, "[WARNING] Found zero-value. Drop the frame continuously " + std::to_string(n_zero_value_drop_frame) + " times");
					}
					break;
				}

				//
				value = (valueFrameBuffer - minValue) * scale;
				// if (value > 255)
				// {
				// 	value = 255;
				// }
				int ofs_r = 3 * value + 0;
				if (colormapSize <= ofs_r)
					ofs_r = colormapSize - 1;
				int ofs_g = 3 * value + 1;
				if (colormapSize <= ofs_g)
					ofs_g = colormapSize - 1;
				int ofs_b = 3 * value + 2;
				if (colormapSize <= ofs_b)
					ofs_b = colormapSize - 1;
				color = qRgb(colormap[ofs_r], colormap[ofs_g], colormap[ofs_b]);
				if (typeLepton == 3)
				{
					column = (i % PACKET_SIZE_UINT16) - 2 + (myImageWidth / 2) * ((i % (PACKET_SIZE_UINT16 * 2)) / PACKET_SIZE_UINT16);
					row = i / PACKET_SIZE_UINT16 / 2 + ofsRow;
				}
				else
				{
					column = (i % PACKET_SIZE_UINT16) - 2;
					row = i / PACKET_SIZE_UINT16;
				}
				if (mirror)
				{
					column = myImageWidth - column - 1;
				}
				if (valueFrameBuffer == maxTemp)
				{
					max_column = column;
					max_row = row;
				}
				myImage.setPixel(column, row, color);
			}
		}

		if (n_zero_value_drop_frame != 0)
		{
			log_message(8, "[WARNING] Found zero-value. Drop the frame continuously " + std::to_string(n_zero_value_drop_frame) + " times [RECOVERED]");
			n_zero_value_drop_frame = 0;
		}

		//crosshair
		for (int i = -1; i < 2; i++)
		{
			if (max_column + i >= 0 && max_column < myImageHeight)
			{
				myImage.setPixel((max_column + i), max_row, crosshair);
			}
			if (max_row + i >= 0 && max_row < myImageWidth)
			{
				myImage.setPixel(max_column, (max_row + i), crosshair);
			}
		}

		//capture over_heat
		if (over_heat && auto_capture)
		{
			printf("starting capture...\n");
			capture();
		}

		//lets emit the signal for update
		emit updateImage(myImage);
	}

	//finally, close SPI port just bcuz
	SpiClosePort(0);
}

void LeptonThread::capture()
{
	// homedir
	const char *homedir;
	if ((homedir = getenv("HOME")) == NULL)
	{
		homedir = getpwuid(getuid())->pw_dir;
	}

	printf("homedir: %s\n", homedir);

	// capture time
	time_t now = time(NULL);

	printf("capture: %ld\n", now);

	// img_path
	char img_path[30];
	strcpy(img_path, homedir);
	strcat(img_path, "/capture.jpg");

	printf("img_path: %s\n", img_path);

	// save image
	bool isSave = myImage.save(img_path, "jpeg", 100);

	printf("capture: %s\n", isSave ? "true" : "false");

	// json_path
	char json_path[30];
	strcpy(json_path, homedir);
	strcat(json_path, "/.doorman.json");

	printf("json_path: %s\n", json_path);

	// save json
	FILE *f = fopen(json_path, "w");
	if (f == NULL)
	{
		printf("Error opening file! %s\n", json_path);
		return;
	}
	fprintf(f, "{\"filename\":\"%ld\",\"uploaded\":false}", now);
	fclose(f);

	// upload to s3
	char filename[20];
	sprintf(filename, "%ld.jpg", now);

	char aws[100];
	strcpy(aws, "aws s3 cp ");
	strcat(aws, img_path);
	strcat(aws, " s3://deeplens-doorman-demo/thermal/");
	strcat(aws, filename);
	strcat(aws, " --acl public-read");

	printf("cmd: %s\n", aws);

	system(aws);
}

void LeptonThread::performFFC()
{
	//perform FFC
	lepton_perform_ffc();
}

void LeptonThread::log_message(uint16_t level, std::string msg)
{
	if (level <= loglevel)
	{
		std::cerr << msg << std::endl;
	}
}
