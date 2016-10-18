#include "controllers/SIT2FLC.h"

void odometryCallback(const nav_msgs::OdometryConstPtr& odometry_msg){
    pose << odometry_msg->pose.pose.position.x, odometry_msg->pose.pose.position.y, odometry_msg->pose.pose.position.z, odometry_msg->pose.pose.orientation.z;
    velocity << odometry_msg->twist.twist.linear.x, odometry_msg->twist.twist.linear.y, odometry_msg->twist.twist.linear.z, odometry_msg->twist.twist.angular.z;
    //cout << "[SIT2FLC] position: " << pose.transpose() << endl;
}

void trajectoryCallback(const QuaternionStamped& trajectory_msg){
    pose_d << trajectory_msg.quaternion.x, trajectory_msg.quaternion.y, trajectory_msg.quaternion.z, trajectory_msg.quaternion.w;
    //cout << "[SIT2FLC] position: " << pose_d.transpose() << endl;
}

void trajectoryVelocityCallback(const QuaternionStamped& velocity_msg){
    velocity_d << velocity_msg.quaternion.x, velocity_msg.quaternion.y, velocity_msg.quaternion.z, velocity_msg.quaternion.w;
    //cout << "[SI-IT2-FLC] velocity_d: " << velocity_d.transpose() << endl;
}

void dynamicReconfigureCallback(controllers::setSIT2FLCConfig &config, uint32_t level){
    k_p = config.k_p;
    k_i = config.k_i;
    k_d = config.k_d;

    alpha_p = config.alpha_p;
    alpha_i = config.alpha_i;
    alpha_d = config.alpha_d;
}

// Constructor
SIT2FLC::SIT2FLC(int argc, char** argv){
    ros::init(argc, argv, "SI_T2_FPID");
    ros::NodeHandle node_handle;

    odometry_subscriber = node_handle.subscribe("/uav/odometry", 1, odometryCallback);
    trajectory_subscriber = node_handle.subscribe("/uav/trajectory", 1, trajectoryCallback);
    trajectory_velocity_subscriber = node_handle.subscribe("/uav/trajectory_velocity", 1, trajectoryVelocityCallback);

    velocity_publisher = node_handle.advertise<geometry_msgs::Quaternion>("/uav/command_velocity", 1);

    pose << 0, 0, 0, 0;
    pose_d << 0, 0, 0, 0;
    velocity << 0, 0, 0, 0;
    velocity_d << 0, 0, 0, 0;

    phi_i << 0, 0, 0, 0;
    phi0_d_old << 0, 0, 0, 0;

    if(argc > 1){
        k_p = atof(argv[1]);
        k_i = atof(argv[2]);
        k_d = atof(argv[3]);
        alpha_p = atof(argv[4]);
        alpha_i = atof(argv[5]);
        alpha_d = atof(argv[6]);
    }
    else{
        k_p = 2.0;
        k_i = 1.0;
        k_d = 0.017;
        alpha_p = 0.2;
        alpha_i = 0.1;
        alpha_d = 0.6;
    }

    k_e = (double)1 / E_MAX;
}

// Destructor
SIT2FLC::~SIT2FLC(){
    ros::shutdown();
    exit(0);
}

void SIT2FLC::run(){
    dynamic_reconfigure::Server<controllers::setSIT2FLCConfig> server;
    dynamic_reconfigure::Server<controllers::setSIT2FLCConfig>::CallbackType f;
    f = boost::bind(&dynamicReconfigureCallback, _1, _2);
    server.setCallback(f);

    double dt = (double)1/100;
    ros::Rate rate(100);
    while(ros::ok()){
      rate.sleep();
      ros::spinOnce();

      //ros::Time begin = ros::Time::now();

      if(pose_d(2) > 0){ // no command
          error = pose_d - pose;
          sigma = k_e * error;

          sigma << max(-1.0, min(1.0, sigma(0))), max(-1.0, min(1.0, sigma(1))), max(-1.0, min(1.0, sigma(2))), max(-1.0, min(1.0, sigma(3)));

          phi_p << T2(sigma(0), alpha_p), T2(sigma(1), alpha_p), T2(sigma(2), alpha_p), T2(sigma(3), alpha_p);
          phi0_i << T2(sigma(0), alpha_i), T2(sigma(1), alpha_i), T2(sigma(2), alpha_i), T2(sigma(3), alpha_p);
          phi0_d << T2(sigma(0), alpha_d), T2(sigma(1), alpha_d), T2(sigma(2), alpha_d), T2(sigma(3), alpha_p);

          phi_i += phi0_i * dt;
          phi_d = (phi0_d - phi0_d_old) / dt;
          phi0_d_old = phi0_d;

          geometry_msgs::Quaternion velocity_msg;
          velocity_msg.x = (k_p * phi_p(0) + k_i * phi_i(0) + k_d * phi_d(0)) / k_e;
          velocity_msg.y = (k_p * phi_p(1) + k_i * phi_i(1) + k_d * phi_d(1)) / k_e;
          velocity_msg.z = (k_p * phi_p(2) + k_i * phi_i(2) + k_d * phi_d(2)) / k_e;
          velocity_msg.w = 0;//(k_p * phi_p(3) + k_i * phi_i(3) + k_d * phi_d(3)) / k_e;
          velocity_publisher.publish(velocity_msg);
      }

      //cout << "[SIT2FLC]: " << (ros::Time::now() - begin) << endl;
    }
}

double SIT2FLC::T2(double x1, double m){
    double m3 = 1 - m;
    double m2 = m;
    double m1 = 1 - m;
    Matrix3d d1;
    d1 << -3.5, -1, 0, -1, 0, 1, 0, 1, 3.5;
    Vector3d cup1;
    cup1 << -1, 0, 1;
    Vector3d aitu;
    aitu = beta(x1, d1);
    Vector3d aitl;
    aitl << aitu(0) * m1, aitu(1) * m2, aitu(2) * m3;

    Vector2d aitl1;
    Vector2d aitu1;
    Vector2d cup;

    if(aitu(0) > 0){
        aitl1 << aitl(0), aitl(1);
        aitu1 << aitu(0), aitu(1);
        cup << cup1(0), cup1(1);
    }else{
        aitl1 << aitl(1), aitl(2);
        aitu1 << aitu(1), aitu(2);
        cup << cup1(1), cup1(2);
    }

    double yl = (cup(0) * aitu1(0) + cup(1) * aitl1(1)) / (aitu1(0) + aitl1(1));
    double yr = (cup(0) * aitl1(0) + cup(1) * aitu1(1)) / (aitl1(0) + aitu1(1));
    double y = (yl + yr) * 0.5;

    if(isnan(yl) || isnan(yr))
        y = 1;
    return y;
}

Vector3d SIT2FLC::beta(double x, Matrix3d& d){
    int k = 0;
    Vector3d ait;
    ait << 0, 0, 0;

    Vector2i y1;

    for(int i = 0; i < 3; ++i){
        if((x <= d(i, k) && i == 0) || (i == 2 && x >= d(i, k + 2)))
            ait(i) = 1;
        else{
            if(x >= d(i, k) && x <= d(i, k + 1)){
                y1 << 0, 1;
                ait(i) = ((x - d(i, k + 1)) / ((d(i, k + 1) - d(i, k)) / (y1(1) - y1(0)))) + y1(1);
            }else{
                if(x >= d(i, k + 1) && x <= d(i, k + 2)){
                    y1 << 1, 0;
                    ait(i) = ((x - d(i, k + 2)) / ((d(i, k + 2) - d(i, k + 1)) / (y1(1) - y1(0)))) + y1(1);
                }else
                    ait(i) = 0;
            }
        }
    }

    return ait;
}

int main(int argc, char** argv){
    cout << "[SIT2FLC] SI-T2-FLC position controller is running..." << endl;

    SIT2FLC* controller = new SIT2FLC(argc, argv);

    controller->run();
}
