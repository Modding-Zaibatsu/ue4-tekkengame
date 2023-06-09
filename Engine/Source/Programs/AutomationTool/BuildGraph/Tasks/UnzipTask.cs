﻿using AutomationTool;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;
using UnrealBuildTool;

namespace BuildGraph.Tasks
{
	/// <summary>
	/// Parameters for a zip task
	/// </summary>
	public class UnzipTaskParameters
	{
		/// <summary>
		/// The directory to copy from
		/// </summary>
		[TaskParameter]
		public string ZipFile;

		/// <summary>
		/// Output directory for the extracted files
		/// </summary>
		[TaskParameter]
		public string ToDir;

		/// <summary>
		/// Tag to be applied to build products of this task
		/// </summary>
		[TaskParameter(Optional = true, ValidationType = TaskParameterValidationType.TagList)]
		public string Tag;
	}

	/// <summary>
	/// Task which creates a zip archive
	/// </summary>
	[TaskElement("Unzip", typeof(UnzipTaskParameters))]
	public class UnzipTask : CustomTask
	{
		/// <summary>
		/// Parameters for this task
		/// </summary>
		UnzipTaskParameters Parameters;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InParameters">Parameters for this task</param>
		public UnzipTask(UnzipTaskParameters InParameters)
		{
			Parameters = InParameters;
		}

		/// <summary>
		/// Execute the task.
		/// </summary>
		/// <param name="Job">Information about the current job</param>
		/// <param name="BuildProducts">Set of build products produced by this node.</param>
		/// <param name="TagNameToFileSet">Mapping from tag names to the set of files they include</param>
		/// <returns>True if the task succeeded</returns>
		public override bool Execute(JobContext Job, HashSet<FileReference> BuildProducts, Dictionary<string, HashSet<FileReference>> TagNameToFileSet)
		{
			DirectoryReference ToDir = ResolveDirectory(Parameters.ToDir);

			// Find all the zip files
			IEnumerable<FileReference> ZipFiles = ResolveFilespec(CommandUtils.RootDirectory, Parameters.ZipFile, TagNameToFileSet);

			// Extract the files
			HashSet<FileReference> OutputFiles = new HashSet<FileReference>();
			foreach(FileReference ZipFile in ZipFiles)
			{
				OutputFiles.UnionWith(CommandUtils.UnzipFiles(ZipFile.FullName, ToDir.FullName).Select(x => new FileReference(x)));
			}

			// Apply the optional tag to the produced archive
			foreach(string TagName in FindTagNamesFromList(Parameters.Tag))
			{
				FindOrAddTagSet(TagNameToFileSet, TagName).UnionWith(OutputFiles);
			}

			// Add the archive to the set of build products
			BuildProducts.UnionWith(OutputFiles);
			return true;
		}

		/// <summary>
		/// Output this task out to an XML writer.
		/// </summary>
		public override void Write(XmlWriter Writer)
		{
			Write(Writer, Parameters);
		}

		/// <summary>
		/// Find all the tags which are used as inputs to this task
		/// </summary>
		/// <returns>The tag names which are read by this task</returns>
		public override IEnumerable<string> FindConsumedTagNames()
		{
			return FindTagNamesFromFilespec(Parameters.ZipFile);
		}

		/// <summary>
		/// Find all the tags which are modified by this task
		/// </summary>
		/// <returns>The tag names which are modified by this task</returns>
		public override IEnumerable<string> FindProducedTagNames()
		{
			return FindTagNamesFromList(Parameters.Tag);
		}
	}
}
