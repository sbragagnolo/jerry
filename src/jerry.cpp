#include "ros/ros.h"
#include "geometry_msgs/Vector3.h"

#include <sstream>

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>



int main(int argc, char **argv)
{
  ros::init(argc, argv, "jerry");
  ros::NodeHandle n;
  ros::Publisher jerry_pub = n.advertise<geometry_msgs::Vector3>("delta_xyt", 1000);
  ros::Rate loop_rate(50);

  char buffer [3];
  int mouse = open (argv[1], 0, O_RDONLY) ;

  ros::Time lastMeasure = ros::Time::now();

  while (ros::ok())  {
	
	ros::Time now;
	geometry_msgs::Vector3 v3;
   
	if (read(mouse, buffer, 3) <= 0) {

		printf ("Error reading mouse! ");
		return -1;
	}

	v3.x = (float)buffer[1];
	v3.y = (float)buffer[2];
	
	now = ros::Time::now();
	v3.z = (now - lastMeasure).toSec();
	lastMeasure = now;
	
	jerry_pub.publish(v3);
	
	ros::spinOnce();

   	loop_rate.sleep();
   
  }


  return 0;
}

