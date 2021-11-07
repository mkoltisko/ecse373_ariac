#include "ros/ros.h"
#include "std_msgs/String.h"
#include "std_srvs/Trigger.h"
#include "osrf_gear/Order.h"
#include "osrf_gear/GetMaterialLocations.h"
#include "osrf_gear/LogicalCameraImage.h"
#include "osrf_gear/Model.h"
#include "geometry_msgs/Pose.h"
#include <vector>
#include <string>
#include <map>

//Lab 6
#include "ur_kinematics/ur_kin.h"
#include "sensor_msgs/JointState.h"
#include "trajectory_msgs/JointTrajectory.h"

//TRANSFORMS:
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <tf2_ros/transform_listener.h>

//ActionServers
#include "actionlib/client/simple_action_client.h"
#include "actionlib/client/terminal_state.h"
#include "control_msgs/FollowJointTrajectoryAction.h"

//Global Variables
//Order tracking
int order_count = 0;
std::vector<osrf_gear::Order> order_vector;

//Items
std::map<std::string, std::vector<osrf_gear::Model>> items_bin;     //Holds products in bins
std::map<std::string, std::vector<osrf_gear::Model>> items_agv;     //Holds products on the conveyerbelts
std::map<std::string, std::vector<osrf_gear::Model>> items_qcs;     //Holds faulty projects (on trays)

//Joint states:
sensor_msgs::JointState joint_states;

//BIN LOCATIONS <ID, y location>
std::map<std::string, double> bin_pos = {
    { "bin4", .383 },
    { "bin5", 1.15 },
    { "bin6", 1.916 }
};


//Helper method to print out a Pose into a useful string.
void printPose(const geometry_msgs::Pose pose){
    ROS_WARN("xyz = (%f, %f, %f)",pose.position.x,pose.position.y,pose.position.z);
    ROS_WARN("wxyz = (%f, %f, %f, %f)",pose.orientation.w,pose.orientation.x,pose.orientation.y,pose.orientation.z);
}

// Store the order whenever it is received.
void orderCallback(const osrf_gear::Order::ConstPtr &msg)
{
    ROS_INFO("Order %d Received.", ++order_count);
    order_vector.push_back(*msg);
}

// Camera callback to store the product (models) based on types.
void cameraCallback(
    const osrf_gear::LogicalCameraImage::ConstPtr &msg, 
    std::map<std::string, std::vector<osrf_gear::Model>> *itemMap       //Must use pointers to avoid segfault
){
    if(msg -> models.empty()) {
        // Return if no parts below.
        return;
    }
        
    //Then get the type if there are any
    std::string type = msg -> models.front().type;

    //For QC cameras, if no type, set type to faulty:
    if(type.empty()){
        type = "faulty";
    }


    //Create a copy of each of the models:
    std::vector<osrf_gear::Model> product_models;
    for (osrf_gear::Model product_model : msg -> models){
        product_models.push_back(osrf_gear::Model(product_model));
    }
    (*itemMap)[type] = product_models;
}

void armJointCallback(
    const sensor_msgs::JointState::ConstPtr &msg
){
    joint_states = sensor_msgs::JointState(*msg);
}

int valid_solution (double possible_sol[8][6]){
    int op_sol = 0;
    //CAUSES SEGFAULT:
     for(int i = 0; i < 8; i++){
         ROS_WARN("%f, %f, %f, %f, %f, %f", possible_sol[i][0],possible_sol[i][1],possible_sol[i][2],possible_sol[i][3],possible_sol[i][4],possible_sol[i][5]);
	 
	if(possible_sol[i][1] > 3.14159){
            op_sol++;
        }
        else{
            return op_sol;
        }

     }
    // possible_sol[i]

    // for(auto& solution : possible_sol){
    //     if( solution[0] < -1.57 || 1.57 < solution[0]){
    //         op_sol++;
    //     }
    //     else{
    //         break;
    //     }

    // }

    return 0;
}

int main(int argc, char **argv)
{
    //Initialize ros:
    ros::init(argc, argv, "ariac_comp_node");

    /**
   * NodeHandle is the main access point to communications with the ROS system.
   * The first NodeHandle constructed will fully initialize this node, and the last
   * NodeHandle destructed will close down the node.
   */
    ros::NodeHandle n;

    //Create trigger for the beginning of the competition:
    std_srvs::Trigger begin_comp;
    ros::ServiceClient begin_client = n.serviceClient<std_srvs::Trigger>("/ariac/start_competition");

    actionlib::SimpleActionClient<control_msgs::FollowJointTrajectoryAction>
        trajectory_as("/ariac/arm1/arm/follow_joint_trajectory", true);
    control_msgs::FollowJointTrajectoryAction joint_trajectory_as;

    //Subscribe to cameras over product bins:
        //ITEMS:
    ros::Subscriber camera_sub_b1 = n.subscribe<osrf_gear::LogicalCameraImage>("/ariac/logical_camera_bin1" , 1000, boost::bind(cameraCallback, _1, &items_bin));
    ros::Subscriber camera_sub_b2 = n.subscribe<osrf_gear::LogicalCameraImage>("/ariac/logical_camera_bin2" , 1000, boost::bind(cameraCallback, _1, &items_bin));
    ros::Subscriber camera_sub_b3 = n.subscribe<osrf_gear::LogicalCameraImage>("/ariac/logical_camera_bin3" , 1000, boost::bind(cameraCallback, _1, &items_bin));
    ros::Subscriber camera_sub_b4 = n.subscribe<osrf_gear::LogicalCameraImage>("/ariac/logical_camera_bin4" , 1000, boost::bind(cameraCallback, _1, &items_bin));
    ros::Subscriber camera_sub_b5 = n.subscribe<osrf_gear::LogicalCameraImage>("/ariac/logical_camera_bin5" , 1000, boost::bind(cameraCallback, _1, &items_bin));
    ros::Subscriber camera_sub_b6 = n.subscribe<osrf_gear::LogicalCameraImage>("/ariac/logical_camera_bin6" , 1000, boost::bind(cameraCallback, _1, &items_bin));
        //AGV:
    ros::Subscriber camera_sub_a1 = n.subscribe<osrf_gear::LogicalCameraImage>("/ariac/logical_camera_agv1" , 1000, boost::bind(cameraCallback, _1, &items_agv));
    ros::Subscriber camera_sub_a2 = n.subscribe<osrf_gear::LogicalCameraImage>("/ariac/logical_camera_agv2" , 1000, boost::bind(cameraCallback, _1, &items_agv));
        //TRAYS:
    ros::Subscriber camera_sub_q1 = n.subscribe<osrf_gear::LogicalCameraImage>("/ariac/quality_control_sensor_1" , 1000, boost::bind(cameraCallback, _1, &items_qcs));
    ros::Subscriber camera_sub_q2 = n.subscribe<osrf_gear::LogicalCameraImage>("/ariac/quality_control_sensor_2" , 1000, boost::bind(cameraCallback, _1, &items_qcs));
    
    //Subscribe to incoming orders:
    ros::Subscriber orderSub = n.subscribe("/ariac/orders", 1000, orderCallback);

    //Create the material location service for determining location of products
    ros::ServiceClient material_location_client = n.serviceClient<osrf_gear::GetMaterialLocations>("/ariac/material_locations");


    //Subscribe to joint states:
    ros::Subscriber joint_states_h = n.subscribe("/ariac/arm1/joint_states", 10, armJointCallback);

    //Publish to a topic:
    ros::Publisher trajectory_mover = n.advertise<trajectory_msgs::JointTrajectory>("/ariac/arm1/arm/command", 10);
    //Begin the competition
    int service_call_succeeded;
    service_call_succeeded = begin_client.call(begin_comp);
    if (!service_call_succeeded)
    {
        //Something went wrong calling the service itself
        ROS_ERROR("Competition service call failed! LOSER!");
        ros::shutdown();
    }
    else
    {
        if (!begin_comp.response.success)
        {   
            //Service was called, but could not be started
            ROS_WARN("Start Failure: %s", begin_comp.response.message.c_str());
        }
        else
        {
            //Service was called and competition started successfully
            ROS_INFO("Start Success: %s", begin_comp.response.message.c_str());
        }
    }

    //Clear out the orders in case of previous artifacting.
    order_vector.clear();

    //Define checks
    ros::Rate loop_rate(10);

    int msg_count = 0;

    while (ros::ok())
    {
        //Look and see if there are any waiting:
        if(!order_vector.empty()){
            ROS_INFO("------------------------");
            ROS_INFO("Processing Order Begin: ");

            /**
             * FIRST CODE TRY, loops through each order, and each subsequent product, locations, etc. Kept for future assignments
             */
            //Loop through each shipment
            // for(osrf_gear::Shipment shipment : order_vector.front().shipments){
            //     //And each product within
            //     for(osrf_gear::Product product : shipment.products){
            //         std::string product_type = product.type.c_str();
            //         ROS_INFO("%s", product_type);
            //         // std_srvs::Trigger mat_loc;
            //         osrf_gear::GetMaterialLocations srv;
            //         srv.request.material_type = product_type;
            //         // int call_succeeded = material_location_client.call(srv);
            //         // ROS_INFO("%d", call_succeeded);
            //         if(material_location_client.call(srv)){
            //             for(osrf_gear::StorageUnit unit : srv.response.storage_units){
            //                 ROS_INFO("%s", unit.unit_id.c_str());
            //             }
            //         }
            //     }
            // }

            //Get the first product (first order's first shipment's first product)
            osrf_gear::Product first_product = order_vector.front().shipments.front().products.front();
            
            //Get the type
            std::string product_type = first_product.type;
            ROS_INFO("Product Type : %s", product_type.c_str());
            
            //Create a request to the material location service:
            osrf_gear::GetMaterialLocations srv;
            srv.request.material_type = product_type;

            //Call the request
            if(material_location_client.call(srv)){
                //Were able to find a product location:
                std::string product_location = srv.response.storage_units.front().unit_id;
                ROS_INFO("Located In   : %s", product_location.c_str());
                osrf_gear::Model first_model = items_bin[product_type.c_str()].front();
                // geometry_msgs::Pose first_pose = items_bin[product_type.c_str()][7];
                // geometry_msgs::PoseStamped first_pose = items_bin[product_type.c_str()].front();
                //Output the pose
                ROS_WARN("Product Position:");
                printPose(first_model.pose);
                // printPose(first_pose);




                // //MOVE TO LOCATION
                // //Trajectory Command Message:
                trajectory_msgs::JointTrajectory act_joint_trajectory;

                //     //Bootsrap
                 act_joint_trajectory.header.seq = msg_count++;
                 act_joint_trajectory.header.stamp = ros::Time::now();
                 act_joint_trajectory.header.frame_id = "/world";    //TODO World correct?

                 act_joint_trajectory.joint_names.clear();
		 act_joint_trajectory.joint_names.push_back("linear_arm_actuator_joint");
		 act_joint_trajectory.joint_names.push_back("shoulder_pan_joint");
                 act_joint_trajectory.joint_names.push_back("shoulder_lift_joint");
                 act_joint_trajectory.joint_names.push_back("elbow_joint");
                 act_joint_trajectory.joint_names.push_back("wrist_1_joint");
                 act_joint_trajectory.joint_names.push_back("wrist_2_joint");
                 act_joint_trajectory.joint_names.push_back("wrist_3_joint");
                
                 //act_joint_trajectory.points.clear();
                 act_joint_trajectory.points.resize(1);

                // // DON'T MOVE ARM: 
                 //act_joint_trajectory.points[0].positions.resize(act_joint_trajectory.joint_names.size());
                 //for (int indy =0; indy < act_joint_trajectory.joint_names.size(); indy++){
                 //    for(int indz = 0; indz < joint_states.name.size(); indz++){
                 //        if(act_joint_trajectory.joint_names[indy] == joint_states.name[indz]) {
                 //           act_joint_trajectory.points[0].positions[indy] = joint_states.position[indz];
                 //            break;
                 //        }
                 //   }
                 //}

                // //Set waypoint size:
                //act_joint_trajectory.points[0].positions[1] = bin_pos[product_location];     //Where the linear actuator is now, replace with bin location
		act_joint_trajectory.points[0].positions[0] = joint_states.position[1];
		act_joint_trajectory.points[0].positions[1] = 3.14159;
		act_joint_trajectory.points[0].positions[2] = -1*1.45; // -90 plus a little bit
		act_joint_trajectory.points[0].positions[3] = -1*3.05;
		act_joint_trajectory.points[0].positions[4] = 0;
		act_joint_trajectory.points[0].positions[5] = 0;
                act_joint_trajectory.points[0].positions[6] = 0;

		act_joint_trajectory.points[0].time_from_start = ros::Duration(1.0);
                
                 joint_trajectory_as.action_goal.goal.trajectory = act_joint_trajectory;

                 actionlib::SimpleClientGoalState act_state = \
                     trajectory_as.sendGoalAndWait(joint_trajectory_as.action_goal.goal, \
                     ros::Duration(30.0), ros::Duration(30.0));
                 ROS_INFO("Action Server returned with status [%i] %s", act_state.state_, act_state.toString().c_str());



                
                //LOOK FOR THE TRANSFORM
                tf2_ros::Buffer tf_buffer;
                tf2_ros::TransformListener tf2_listener(tf_buffer);
                // tf2_ros::TransformListener tf2_listener(tf_buffer2);
                
                geometry_msgs::TransformStamped camera_to_base;                
                camera_to_base = tf_buffer.lookupTransform("arm1_base_link", "logical_camera_bin4_frame", ros::Time(0), ros::Duration(1.0) );
                
                //Perform the transpose:
                geometry_msgs::Pose new_pose;
                geometry_msgs::Pose model_pose = first_model.pose;
                tf2::doTransform(model_pose, new_pose, camera_to_base); // robot_pose is the PoseStamped I want to transform
                // printPose(tempPose);

                // TRANSFORM TO WORLD COORDINATES FOR DEBUG
                // geometry_msgs::TransformStamped camera_to_world;
                // geometry_msgs::Pose tempPose2;
                // camera_to_world = tf_buffer.lookupTransform("world", "logical_camera_bin4_frame", ros::Time(0), ros::Duration(1.0) );
                // tf2::doTransform(m_pose, tempPose2, camera_to_world); // robot_pose is the PoseStamped I want to transform
                // printPose(tempPose2);


                //LAB 6 Arm Movement:
                double T_des[4][4] = {0.0};
                double q_des[8][6] = {0.0};

                //Where is our product?
                    // WHAT WE ARE SHOOTING FOR
                T_des[0][3] = new_pose.position.x;
                T_des[1][3] = new_pose.position.y;
                T_des[2][3] = new_pose.position.z + .15;     //Place slightly above model
                T_des[3][3] = 1.0;
                    //End effector ROTATION
                T_des[2][0] = -1.0;
                T_des[0][1] = -1.0;
                T_des[1][2] = 1.0;

                //InverseKinematics to find joint angles:
                int num_sols;
                num_sols = ur_kinematics::inverse(&T_des[0][0], &q_des[0][0], 0.0);
                ROS_INFO("%d", num_sols);

                //Trajectory Command Message:
                trajectory_msgs::JointTrajectory joint_trajectory;

                    //Bootsrap
                joint_trajectory.header.seq = msg_count++;
                joint_trajectory.header.stamp = ros::Time::now();
                joint_trajectory.header.frame_id = "/world";    //TODO World correct?

                joint_trajectory.joint_names.clear();
                joint_trajectory.joint_names.push_back("linear_arm_actuator_joint");
                joint_trajectory.joint_names.push_back("shoulder_pan_joint");
                joint_trajectory.joint_names.push_back("shoulder_lift_joint");
                joint_trajectory.joint_names.push_back("elbow_joint");
                joint_trajectory.joint_names.push_back("wrist_1_joint");
                joint_trajectory.joint_names.push_back("wrist_2_joint");
                joint_trajectory.joint_names.push_back("wrist_3_joint");
                
                joint_trajectory.points.clear();
                joint_trajectory.points.resize(1);

                // POINT 1: NOT NECESSARY:
                // joint_trajectory.points[0].time_from_start = ros::Duration(0.0);

                // // populate startpooint, point 0, with current position given by joint_states 
                // joint_trajectory.points[0].positions.resize(joint_trajectory.joint_names.size());
                // for (int indy =0; indy < joint_trajectory.joint_names.size(); indy++){
                //     for(int indz = 0; indz < joint_states.name.size(); indz++){
                //         if(joint_trajectory.joint_names[indy] == joint_states.name[indz]) {
                //             joint_trajectory.points[0].positions[indy] = joint_states.position[indz];
                //             break;
                //         }
                //     }
                // }


                // time to start moving from startpoint
                // we want to move immediately so 0 seconds

                // SET WAYPOINT TARGET
                int q_des_indx = valid_solution(q_des);
                ROS_WARN("%d", q_des_indx);
                
                //Set waypoint size:
                joint_trajectory.points[0].positions.resize(joint_trajectory.joint_names.size());
                joint_trajectory.points[0].positions[0] = joint_states.position[1];     //Where the linear actuator is now, replace with bin location

                for(int indy = 0; indy < 6; indy++) {
                    joint_trajectory.points[0].positions[indy+1] = q_des[q_des_indx][indy];	
			
                }
		const double pi = 3.14159; // 180 deg
		const double pi2 = pi/2;  // 90 deg
		const double pi4 = pi/4;  // 45 deg
		joint_trajectory.points[0].positions[0] = joint_states.position[1]; // linear arm actuator, position on its own belt
		//joint_trajectory.points[0].positions[1] = 0; // shoulder pan swivel base, positive CCW
		//joint_trajectory.points[0].positions[2] = (joint_trajectory.points[0].positions[2] > pi) ? joint_trajectory.points[0].positions[2] - 2*pi : joint_trajectory.points[0].positions[2]; // shoulder lift, pitch
		//joint_trajectory.points[0].positions[3] = (joint_trajectory.points[0].positions[3] > pi) ?0; // elbow
			//joint_trajectory.points[0].positions[3] - 2*pi : joint_trajectory.points[0].positions[2];		
		//joint_trajectory.points[0].positions[4] = (joint_trajectory.points[0].positions[4] > pi) ?0; // wrist 1
			//joint_trajectory.points[0].positions[4] - 2*pi : joint_trajectory.points[0].positions[2];
		//joint_trajectory.points[0].positions[5] = (joint_trajectory.points[0].positions[5] > pi) ?0; // wrist 2 
			//joint_trajectory.points[0].positions[5] - 2*pi : joint_trajectory.points[0].positions[2];
		//joint_trajectory.points[0].positions[6] = (joint_trajectory.points[0].positions[6] > pi) ?; // wrist 3 orient vacuum
			//joint_trajectory.points[0].positions[2] - 2*pi : joint_trajectory.points[0].positions[2];

                joint_trajectory.points[0].time_from_start = ros::Duration(2.0);
                
                // FOR PUBLISHING TO TOPIC
                // trajectory_mover.publish(joint_trajectory);
                
                joint_trajectory_as.action_goal.goal.trajectory = joint_trajectory;
                actionlib::SimpleClientGoalState state = \
                    trajectory_as.sendGoalAndWait(joint_trajectory_as.action_goal.goal, \
                    ros::Duration(30.0), ros::Duration(30.0));
                ROS_INFO("Action Server returned with status [%i] %s", state.state_, state.toString().c_str());
            }
            else{
                //Call failed
            }

            order_vector.erase(order_vector.begin());
            ROS_INFO("Processing Order End:   ");
            ROS_INFO("------------------------");
        }
        ros::spinOnce();
        loop_rate.sleep();
    }

    return 0;
}
