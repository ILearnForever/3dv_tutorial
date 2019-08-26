#include "bundle_adjustment.hpp"

int main()
{
    // c.f. You need to run 'image_formation.cpp' to generate point observation.
    const char* input = "image_formation%d.xyz";
    int input_num = 5;
    double f = 1000, cx = 320, cy= 240;

    // Load 2D points observed from multiple views
    std::vector<std::vector<cv::Point2d>> xs;
    for (int i = 0; i < input_num; i++)
    {
        FILE* fin = fopen(cv::format(input, i).c_str(), "rt");
        if (fin == NULL) return -1;
        std::vector<cv::Point2d> pts;
        while (!feof(fin))
        {
            double x, y, w;
            if (fscanf(fin, "%lf %lf %lf", &x, &y, &w) == 3)
                pts.push_back(cv::Point2d(x, y));
        }
        fclose(fin);
        xs.push_back(pts);
        if (xs.front().size() != xs.back().size()) return -1;
    }

    // Assumption
    // - All cameras have the same and known camera matrix.
    // - All points are visible on all camera views.

    // 1) Select the best pair (skipped because all points are visible on all images)

    // 2) Estimate relative pose of the initial two views (epipolar geometry)
    cv::Mat F = cv::findFundamentalMat(xs[0], xs[1], cv::FM_8POINT);
    cv::Mat K = (cv::Mat_<double>(3, 3) << f, 0, cx, 0, f, cy, 0, 0, 1);
    cv::Mat E = K.t() * F * K, R, t;
    cv::recoverPose(E, xs[0], xs[1], K, R, t);

    std::vector<cv::Vec6d> views(xs.size());
    cv::Mat rvec;
    cv::Rodrigues(R, rvec);
    views[1] = (rvec.at<double>(0), rvec.at<double>(1), rvec.at<double>(2), t.at<double>(0), t.at<double>(1), t.at<double>(2));

    // 3) Reconstruct 3D points of the initial two views (triangulation)
    cv::Mat Rt;
    cv::hconcat(R, t, Rt);
    cv::Mat P0 = K * cv::Mat::eye(3, 4, CV_64F);
    cv::Mat P1 = K * Rt, X;
    cv::triangulatePoints(P0, P1, xs[0], xs[1], X);

    std::vector<cv::Point3d> Xs(X.cols);
    X.row(0) = X.row(0) / X.row(3);
    X.row(1) = X.row(1) / X.row(3);
    X.row(2) = X.row(2) / X.row(3);
    X.row(3) = 1;
    for (int c = 0; c < X.cols; c++)
        Xs[c] = cv::Point3d(X.col(c).rowRange(0, 3));

    // Push constraints of two views
    ceres::Problem ba;
    for (size_t j = 0; j < 2; j++)
    {
        for (size_t i = 0; i < xs[j].size(); i++)
        {
            ceres::CostFunction* cost_func = ReprojectionError::create(xs[j][i], f, cv::Point2d(cx, cy));
            double* view = (double*)(&(views[j]));
            double* X = (double*)(&(Xs[i]));
            ba.AddResidualBlock(cost_func, NULL, view, X);
        }
    }

    // Incrementally add more views
    ceres::Solver::Options options;
    options.linear_solver_type = ceres::ITERATIVE_SCHUR;
    options.num_threads = 8;
    options.minimizer_progress_to_stdout = true;
    ceres::Solver::Summary summary;
    for (size_t j = 2; j < xs.size(); j++)
    {
        // 4) Select the next image to add (skipped because all points are visible on all images)

        // 5) Estimate relative pose of the next view (PnP)
        cv::solvePnP(Xs, xs[j], K, cv::noArray(), rvec, t);
        views[j] = (rvec.at<double>(0), rvec.at<double>(1), rvec.at<double>(2), t.at<double>(0), t.at<double>(1), t.at<double>(2));

        // 6) Reconstruct newly observed 3D points (triangulation; skipped because all points are visible on all images)

        // 7) Optimize camera pose and 3D points (bundle adjustment)
        for (size_t i = 0; i < xs[j].size(); i++)
        {
            ceres::CostFunction* cost_func = ReprojectionError::create(xs[j][i], f, cv::Point2d(cx, cy));
            double* view = (double*)(&(views[j]));
            double* X = (double*)(&(Xs[i]));
            ba.AddResidualBlock(cost_func, NULL, view, X);
        }
        ceres::Solve(options, &ba, &summary);
    }

    // Store the 3D points to an XYZ file
    FILE* fpts = fopen("bundle_adjustment_inc(point).xyz", "wt");
    if (fpts == NULL) return -1;
    for (size_t i = 0; i < Xs.size(); i++)
        fprintf(fpts, "%f %f %f\n", Xs[i].x, Xs[i].y, Xs[i].z);
    fclose(fpts);

    // Store the camera poses to an XYZ file 
    FILE* fcam = fopen("bundle_adjustment_inc(camera).xyz", "wt");
    if (fcam == NULL) return -1;
    for (size_t j = 0; j < views.size(); j++)
    {
        cv::Vec3d rvec(views[j][0], views[j][1], views[j][2]), t(views[j][3], views[j][4], views[j][5]);
        cv::Matx33d R;
        cv::Rodrigues(rvec, R);
        cv::Vec3d p = -R.t() * t;
        fprintf(fcam, "%f %f %f\n", p[0], p[1], p[2]);
    }
    fclose(fcam);
    return 0;
}
