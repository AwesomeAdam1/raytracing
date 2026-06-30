#ifndef CAMERA_H
#define CAMERA_H

#include "hittable.h"
#include "raytracing.h"
#include "material.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

class camera {
	public:
		double aspect_ratio      = 1.0;  // Ratio of image width over height
		int    image_width       = 100;  // Rendered image width in pixel count
		int    samples_per_pixel = 10;   // Count of random samples for each pixel
		int    max_depth         = 10;   // Maximum number of ray bounces into scene
		color  background;               // Scene background color

		double vfov = 90;				   // Vertical view angle (field of view)
		point3 lookfrom = point3(0,0,0);   // Point camera is looking from
		point3 lookat   = point3(0,0,-1);  // Point camera is looking at
		vec3   vup      = vec3(0,1,0);     // Camera-relative "up" direction
		
		double defocus_angle = 0;  // Variation angle of rays through each pixel
    	double focus_dist = 10;    // Distance from camera lookfrom point to plane of perfect focus

		bool multithreaded = true; // Render with multiple threads when true

		void render(const hittable& world) {
			initialize();

			std::vector<color> framebuffer(size_t(image_width) * image_height);

			auto start = std::chrono::steady_clock::now();

			if (multithreaded)
				render_threaded(world, framebuffer);
			else
				render_single(world, framebuffer);

			auto end = std::chrono::steady_clock::now();
			double elapsed = std::chrono::duration<double>(end - start).count();

			std::cout << "P3\n" << image_width << ' ' << image_height << "\n255\n";
			for (int j = 0; j < image_height; j++)
				for (int i = 0; i < image_width; i++)
					write_color(std::cout, framebuffer[size_t(j) * image_width + i]);

			std::clog << "\rDone in " << elapsed << "s "
					  << (multithreaded ? "(multithreaded)." : "(single-threaded).") << "          \n";
		}

	private:
		// Render every row one after another on a single thread.
		void render_single(const hittable& world, std::vector<color>& framebuffer) {
			for (int j = 0; j < image_height; j++) {
				std::clog << "\rScanlines remaining: " << (image_height - j) << ' ' << std::flush;
				for (int i = 0; i < image_width; i++)
					framebuffer[size_t(j) * image_width + i] = render_pixel(i, j, world);
			}
		}

		// Render rows in parallel: each thread repeatedly grabs the next unclaimed row.
		void render_threaded(const hittable& world, std::vector<color>& framebuffer) {
			std::atomic<int> next_row(0);
			std::atomic<int> rows_done(0);

			auto worker = [&]() {
				int j;
				while ((j = next_row.fetch_add(1)) < image_height) {
					for (int i = 0; i < image_width; i++)
						framebuffer[size_t(j) * image_width + i] = render_pixel(i, j, world);
					int done = rows_done.fetch_add(1) + 1;
					std::clog << "\rScanlines remaining: " << (image_height - done) << ' ' << std::flush;
				}
			};

			unsigned int thread_count = std::thread::hardware_concurrency();
			if (thread_count == 0)
				thread_count = 1;

			std::vector<std::thread> threads;
			for (unsigned int t = 0; t < thread_count; t++)
				threads.emplace_back(worker);
			for (auto& th : threads)
				th.join();
		}

		// Compute the final averaged color for a single pixel.
		color render_pixel(int i, int j, const hittable& world) const {
			color pixel_color(0,0,0);
			for (int sample = 0; sample < samples_per_pixel; sample++) {
				ray r = get_ray(i, j);
				pixel_color += ray_color(r, max_depth, world);
			}
			return pixel_samples_scale * pixel_color;
		}

		int    image_height;         // Rendered image height
		double pixel_samples_scale;  // Color scale factor for a sum of pixel samples
		point3 center;               // Camera center
		point3 pixel00_loc;          // Location of pixel 0, 0
		vec3   pixel_delta_u;        // Offset to pixel to the right
		vec3   pixel_delta_v;        // Offset to pixel below
		vec3   u, v, w;              // Camera frame basis vectors
		vec3   defocus_disk_u;       // Defocus disk horizontal radius
		vec3   defocus_disk_v;       // Defocus disk vertical radius

		void initialize() {
			image_height = int(image_width / aspect_ratio);
			image_height = (image_height < 1) ? 1 : image_height;

			pixel_samples_scale = 1.0 / samples_per_pixel;

			center = lookfrom;

			// Determine viewport dimensions.
			auto theta = degrees_to_radians(vfov);
			auto h = std::tan(theta/2);
			auto viewport_height = 2 * h * focus_dist;
			auto viewport_width = viewport_height * (double(image_width)/image_height);

			// Calculate the u,v,w unit basis vectors for the camera coordinate frame.
			w = unit_vector(lookfrom - lookat);
			u = unit_vector(cross(vup, w));
			v = cross(w, u);
	
			// Calculate the vectors across the horizontal and down the vertical viewport edges.
			vec3 viewport_u = viewport_width * u;    // Vector across viewport horizontal edge
			vec3 viewport_v = viewport_height * -v;  // Vector down viewport vertical edge

			// Calculate the horizontal and vertical delta vectors from pixel to pixel.
			pixel_delta_u = viewport_u / image_width;
			pixel_delta_v = viewport_v / image_height;

			// Calculate the location of the upper left pixel.
			auto viewport_upper_left = center - (focus_dist * w) - viewport_u/2 - viewport_v/2;
			pixel00_loc = viewport_upper_left + 0.5 * (pixel_delta_u + pixel_delta_v);
	
			// Calculate the camera defocus disk basis vectors.
			auto defocus_radius = focus_dist * std::tan(degrees_to_radians(defocus_angle / 2));
			defocus_disk_u = u * defocus_radius;
			defocus_disk_v = v * defocus_radius;
		}	

		ray get_ray(int i, int j) const {
			// Construct a camera ray originating from the defocus disk and directed at a randomly
        	// sampled point around the pixel location i, j.

			auto offset = sample_square();
			auto pixel_sample = pixel00_loc
							+ ((i + offset.x()) * pixel_delta_u)
							+ ((j + offset.y()) * pixel_delta_v);

			auto ray_origin = (defocus_angle <= 0) ? center : defocus_disk_sample();
			auto ray_direction = pixel_sample - ray_origin;
			auto ray_time = random_double();

			return ray(ray_origin, ray_direction, ray_time);
		}

		vec3 sample_square() const {
			// Returns the vector to a random point in the [-.5,-.5]-[+.5,+.5] unit square.
			return vec3(random_double() - 0.5, random_double() - 0.5, 0);
		}

		point3 defocus_disk_sample() const {
			// Returns a random point in the camera defocus disk.
			auto p = random_in_unit_disk();
			return center + (p[0] * defocus_disk_u) + (p[1] * defocus_disk_v);
		}

		color ray_color(const ray& r, int depth, const hittable& world) const {
			if (depth <= 0)
				return color(0,0,0);

			hit_record rec;

			// If the ray hits nothing, return the background color.
			if (!world.hit(r, interval(0.001, infinity), rec))
            return background;

        ray scattered;
        color attenuation;
        color color_from_emission = rec.mat->emitted(rec.u, rec.v, rec.p);

        if (!rec.mat->scatter(r, rec, attenuation, scattered))
            return color_from_emission;

        color color_from_scatter = attenuation * ray_color(scattered, depth-1, world);

        return color_from_emission + color_from_scatter;
		}
};

#endif