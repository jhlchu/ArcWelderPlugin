////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Arc Welder: Anti-Stutter Library
//
// Compresses many G0/G1 commands into G2/G3(arc) commands where possible, ensuring the tool paths stay within the specified resolution.
// This reduces file size and the number of gcodes per second.
//
// Uses the 'Gcode Processor Library' for gcode parsing, position processing, logging, and other various functionality.
//
// Copyright(C) 2020 - Brad Hochgesang
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This program is free software : you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU Affero General Public License for more details.
//
//
// You can contact the author at the following email address: 
// FormerLurker@pm.me
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <string>
#include <vector>
#include <set>
#include "gcode_position.h"
#include "position.h"
#include "gcode_parser.h"
#include "segmented_arc.h"
#include <iostream>
#include <fstream>
#include "array_list.h"
#include "unwritten_command.h"
#include "logger.h"
#include <cmath>

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif


#define DEFAULT_G90_G91_INFLUENCES_EXTREUDER false

static const int segment_statistic_lengths_count = 12;
const double segment_statistic_lengths[] = { 0.002f, 0.005f, 0.01f, 0.05f, 0.1f, 0.5f, 1.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f };

struct segment_statistic {
	segment_statistic(double min_length_mm, double max_length_mm)
	{
		count = 0;
		min_mm = min_length_mm;
		max_mm = max_length_mm;
	}

	double min_mm;
	double max_mm;
	int count;
};

struct source_target_segment_statistics {
	source_target_segment_statistics(const double segment_tracking_lengths[], const int num_lengths, logger* p_logger = NULL) {
		total_length_source = 0;
		total_length_target = 0;
		total_count_source = 0;
		total_count_target = 0;
		max_width = 0;
		max_precision = 3;
		num_segment_tracking_lengths = num_lengths;
		double current_min = 0;
		for (int index = 0; index < num_lengths; index++)
		{
			double current_max = segment_tracking_lengths[index];
			source_segments.push_back(segment_statistic(current_min, segment_tracking_lengths[index]));
			target_segments.push_back(segment_statistic(current_min, segment_tracking_lengths[index]));
			current_min = current_max;
		}
		source_segments.push_back(segment_statistic(current_min, -1.0f));
		target_segments.push_back(segment_statistic(current_min, -1.0f));
		max_width = utilities::get_num_digits(current_min);
		p_logger_ = p_logger;
		logger_type_ = 0;
	}

	std::vector<segment_statistic> source_segments;
	std::vector<segment_statistic> target_segments;
	double total_length_source;
	double total_length_target;
	int max_width;
	int max_precision;
	int total_count_source;
	int total_count_target;
	int num_segment_tracking_lengths;

	void update(double length, bool is_source)
	{
		if (length <= 0)
			return;

		std::vector<segment_statistic>* stats;
		if (is_source)
		{
			total_count_source++;
			total_length_source += length;
			stats = &source_segments;
		}
		else
		{
			total_count_target++;
			total_length_target += length;
			stats = &target_segments;
		}
		for (int index = 0; index < (*stats).size(); index++)
		{
			segment_statistic& stat = (*stats)[index];
			if ( (stat.min_mm <= length && stat.max_mm > length) || (index + 1) == (*stats).size())
			{
				stat.count++;
				break;
			}
		}
	}

	std::string str() const {
		
		//if (p_logger_ != NULL) p_logger_->log(logger_type_, VERBOSE, "Building Segment Statistics.");

		std::stringstream output_stream;
		std::stringstream format_stream;
		const int min_column_size = 8;
		int mm_col_size = max_width + max_precision + 2; // Adding 2 for the mm
		int min_max_label_col_size = 4;
		int percent_col_size = 9;
		int totals_row_label_size = 22;
		int count_col_size;

		// Calculate the count column size
		int max_count = 0;
		//if (p_logger_ != NULL) p_logger_->log(logger_type_, VERBOSE, "Calculating Column Size.");

		for (int index = 0; index < source_segments.size(); index++)
		{
			int source_count = source_segments[index].count;
			int target_count = target_segments[index].count;
			if (max_count < source_count)
			{
				max_count = source_count;
			}
			if (max_count < target_count)
			{
				max_count = target_count;
			}
		}
		// Get the number of digits in the max count
		count_col_size = utilities::get_num_digits(max_count);
		// enforce the minimum of 6
		if (count_col_size < min_column_size)
		{
			count_col_size = min_column_size;
		}

		if (max_precision > 0)
		{
			// We need an extra space in our column for the decimal.
			mm_col_size++;
		}

		// enforce the min column size
		if (mm_col_size < min_column_size)
		{
			mm_col_size = min_column_size;
		}
		// Get the table width
		int table_width = mm_col_size + min_max_label_col_size + mm_col_size + count_col_size + count_col_size + percent_col_size;
		// Add a separator for the statistics
		//output_stream << std::setw(table_width) << std::setfill('-') << "-" << "\n" << std::setfill(' ') ;
		// Output the column headers
		// Center the min and max column.
		output_stream << utilities::center("Min", mm_col_size);
		output_stream << std::setw(min_max_label_col_size) << "";
		output_stream << utilities::center("Max", mm_col_size);
		// right align the source, target and change columns
		output_stream << std::setw(count_col_size) << std::right << "Source";
		output_stream << std::setw(count_col_size) << std::right << "Target";
		output_stream << std::setw(percent_col_size) << std::right << "Change";
		output_stream << "\n";
		output_stream << std::setw(table_width) << std::setfill('-') << "" << std::setfill(' ') << "\n";
		output_stream << std::fixed << std::setprecision(max_precision);
		for (int index = 0; index < source_segments.size(); index++) {
			//extract the necessary variables from the source and target segments
			double min_mm = source_segments[index].min_mm;
			double max_mm = source_segments[index].max_mm;
			int source_count = source_segments[index].count;
			int target_count = target_segments[index].count;
			// Calculate the percent change	and create the string
			// Construct the percent_change_string
			std::string percent_change_string = utilities::get_percent_change_string(source_count, target_count, 1);

			// Create the strings to hold the column values
			std::string min_mm_string;
			std::string max_mm_string;
			std::string source_count_string;
			std::string target_count_string;

			// Clear the format stream and construct the min_mm_string
			format_stream.str(std::string());
			format_stream << std::fixed << std::setprecision(max_precision) << min_mm << "mm";
			min_mm_string = format_stream.str();
			// Clear the format stream and construct the max_mm_string
			format_stream.str(std::string());
			format_stream << std::fixed << std::setprecision(max_precision) << max_mm << "mm";
			max_mm_string = format_stream.str();
			// Clear the format stream and construct the source_count_string
			format_stream.str(std::string());
			format_stream << std::fixed << std::setprecision(0) << source_count;
			source_count_string = format_stream.str();
			// Clear the format stream and construct the target_count_string
			format_stream.str(std::string());
			format_stream << std::fixed << std::setprecision(0) << target_count;
			target_count_string = format_stream.str();
			// The min and max columns and the label need to be handled differently if this is the last item
			if (index == source_segments.size() - 1)
			{
				// If we are on the last setment item, the 'min' value is the max, and there is no end
				// The is because the last item contains the count of all items above the max length provided
				// in the constructor

				// The 'min' column is empty here
				output_stream << std::setw(mm_col_size) << std::internal << "";
				// Add the min/max label
				output_stream << std::setw(min_max_label_col_size) << " >= ";
				// Add the min mm string
				output_stream << std::setw(mm_col_size) << std::internal << min_mm_string;
			}
			else
			{
				//if (p_logger_ != NULL) p_logger_->log(logger_type_, VERBOSE, "Adding row text.");

				// add the 'min' column
				output_stream << std::setw(mm_col_size) << std::internal << min_mm_string;
				// Add the min/max label
				output_stream << std::setw(min_max_label_col_size) << " to ";
				// Add the 'max' column				
				output_stream << std::setw(mm_col_size) << std::internal << max_mm_string;
			}
			// Add the source count
			output_stream << std::setw(count_col_size) << source_count_string;
			// Add the target count
			output_stream << std::setw(count_col_size) << target_count_string;
			// Add the percent change string
			output_stream << std::setw(percent_col_size) << percent_change_string;
			// End the line
			output_stream << "\n";
		}
		// Add the total rows separator
		output_stream << std::setw(table_width) << std::setfill('-') << "" << std::setfill(' ') << "\n";
		// Add the total rows;
		if (utilities::is_equal(total_length_source, total_length_target, 0.001))
		{
			std::string total_distance_string;
			format_stream.str(std::string());
			format_stream << std::fixed << std::setprecision(max_precision) << total_length_source << "mm";
			total_distance_string = format_stream.str();
			output_stream << std::setw(totals_row_label_size) << std::right << "Total distance:";
			output_stream << std::setw(table_width - totals_row_label_size) << std::setfill('.') << std::right << total_distance_string << "\n" << std::setfill(' ');
		}
		else
		{
			// We need to output two different distances (this probably should never happen)
			// Format the total source distance string
			std::string total_source_distance_string;
			format_stream.str(std::string());
			format_stream << std::fixed << std::setprecision(max_precision) << total_length_source << "mm";
			total_source_distance_string = format_stream.str();
			// Add the total source distance row
			output_stream << std::setw(totals_row_label_size) << std::right << "Total distance source:";
			output_stream << std::setw(table_width - totals_row_label_size) << std::setfill('.') << std::right << total_source_distance_string << "\n" << std::setfill(' ');

			// Format the total target distance string			
			std::string total_target_distance_string;
			format_stream.str(std::string());
			format_stream << std::fixed << std::setprecision(max_precision) << total_length_target << "mm";
			total_target_distance_string = format_stream.str();
			// Add the total target distance row
			output_stream << std::setw(totals_row_label_size) << std::right << "Total distance target:";
			output_stream << std::setw(table_width - totals_row_label_size) << std::setfill('.') << std::right << total_target_distance_string << "\n" << std::setfill(' ');
		}

		// Add the total count rows
		// Add the source count
		output_stream << std::setprecision(0) << std::setw(totals_row_label_size) << std::right << "Total count source:";
		output_stream << std::setw(table_width - totals_row_label_size) << std::setfill('.') << std::right << total_count_source << "\n" << std::setfill(' ');
		// Add the target count
		output_stream << std::setw(totals_row_label_size) << std::right << "Total count target:";
		output_stream << std::setw(table_width - totals_row_label_size) << std::setfill('.') << std::right << total_count_target << "\n" << std::setfill(' ');
		// Add the total percent change row
		std::string total_percent_change_string = utilities::get_percent_change_string(total_count_source, total_count_target, 1);
		output_stream << std::setw(totals_row_label_size) << std::right << "Total percent change:";
		output_stream << std::setw(table_width - totals_row_label_size) << std::setfill('.') << std::right << total_percent_change_string << std::setfill(' ');
		std::string output_string = output_stream.str();
		return output_string;
	}

private:
	logger* p_logger_;
	int logger_type_;
};

// Struct to hold the progress, statistics, and return values
struct arc_welder_progress {
	arc_welder_progress() : segment_statistics(segment_statistic_lengths, segment_statistic_lengths_count, NULL) {
		percent_complete = 0.0;
		seconds_elapsed = 0.0;
		seconds_remaining = 0.0;
		gcodes_processed = 0;
		lines_processed = 0;
		points_compressed = 0;
		arcs_created = 0;
		source_file_size = 0;
		source_file_position = 0;
		target_file_size = 0;
		compression_ratio = 0;
		compression_percent = 0;

	}
	double percent_complete;
	double seconds_elapsed;
	double seconds_remaining;
	int gcodes_processed;
	int lines_processed;
	int points_compressed;
	int arcs_created;
	double compression_ratio;
	double compression_percent;
	long source_file_position;
	long source_file_size;
	long target_file_size;
	source_target_segment_statistics segment_statistics;

	std::string str() const {
		std::stringstream stream;
		stream << std::fixed << std::setprecision(2);

		stream << percent_complete << "% complete in " << seconds_elapsed << " seconds with " << seconds_remaining << " seconds remaining.";
		stream << " Gcodes Processed: " << gcodes_processed;
		stream << ", Current Line: " << lines_processed;
		stream << ", Points Compressed: " << points_compressed;
		stream << ", ArcsCreated: " << arcs_created;
		stream << ", Compression Ratio: " << compression_ratio;
		stream << ", Size Reduction: " << compression_percent << "% ";
		return stream.str();
	}
	std::string detail_str() const {
		std::stringstream stream;
		stream << "\n" << "Extrusion/Retraction Counts" << "\n" << segment_statistics.str() << "\n";
		return stream.str();
	}
};

// define the progress callback type 
typedef bool(*progress_callback)(arc_welder_progress, logger* p_logger, int logger_type);

struct arc_welder_results {
	arc_welder_results() : progress()
	{
		success = false;
		cancelled = false;
		message = "";
	}
	bool success;
	bool cancelled;
	std::string message;
	arc_welder_progress progress;
};

class arc_welder
{
public:
	arc_welder(std::string source_path, std::string target_path, logger* log, double resolution_mm, double max_radius, bool g90_g91_influences_extruder, int buffer_size, progress_callback callback = NULL);
	void set_logger_type(int logger_type);
	virtual ~arc_welder();
	arc_welder_results process();
	double notification_period_seconds;
protected:
	virtual bool on_progress_(const arc_welder_progress& progress);
private:
	arc_welder_progress get_progress_(long source_file_position, double start_clock);
	void add_arcwelder_comment_to_target();
	void reset();
	static gcode_position_args get_args_(bool g90_g91_influences_extruder, int buffer_size);
	progress_callback progress_callback_;
	int process_gcode(parsed_command cmd, bool is_end, bool is_reprocess);
	int write_gcode_to_file(std::string gcode);
	std::string get_arc_gcode_relative(double f, const std::string comment);
	std::string get_arc_gcode_absolute(double e, double f, const std::string comment);
	std::string get_comment_for_arc();
	int write_unwritten_gcodes_to_file();
	std::string create_g92_e(double absolute_e);
	std::string source_path_;
	std::string target_path_;
	double resolution_mm_;
	double max_segments_;
	gcode_position_args gcode_position_args_;
	long file_size_;
	int lines_processed_;
	int gcodes_processed_;
	int last_gcode_line_written_;
	int points_compressed_;
	int arcs_created_;
	source_target_segment_statistics segment_statistics_;
	long get_file_size(const std::string& file_path);
	double get_time_elapsed(double start_clock, double end_clock);
	double get_next_update_time() const;
	bool waiting_for_arc_;
	array_list<unwritten_command> unwritten_commands_;
	segmented_arc current_arc_;
	std::ofstream output_file_;

	// We don't care about the printer settings, except for g91 influences extruder.
	gcode_position* p_source_position_;
	double previous_feedrate_;
	bool previous_is_extruder_relative_;
	gcode_parser parser_;
	bool verbose_output_;
	int logger_type_;
	logger* p_logger_;
	bool debug_logging_enabled_;
	bool info_logging_enabled_;
	bool verbose_logging_enabled_;
	bool error_logging_enabled_;

};
