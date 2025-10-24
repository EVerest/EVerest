import os
from typing import Annotated, Dict, List, Optional

import dagger
from dagger import dag, function, object_type


@object_type
class BaseResultType:
    """Base class for result types."""

    container: Annotated[
        dagger.Container, dagger.Doc("Container that ran the operation")
    ] = dagger.field()
    exit_code: Annotated[
        int, dagger.Doc("The exit code of the operation")
    ] = dagger.field()


@object_type
class EverestCI:
    """Functions reused across the EVerest organization for CI/CD purposes."""

    source_dir_container: Annotated[
        str, dagger.Doc("Source directory mount point in the container")
    ] = "/workspace/source"

    @object_type
    class DeploySingleDockerImageResult(BaseResultType):
        """Result of building and pushing a single image."""

        rebuild: Annotated[
            bool, dagger.Doc("Whether the image was rebuilt or not")
        ] = dagger.field()
        image_tags: Annotated[
            List[str], dagger.Doc("Tags of the deployed image with image name")
        ] = dagger.field()
        one_image_tag_short: Annotated[
            str, dagger.Doc("One tag of the deployed image without image name")
        ] = dagger.field()
        one_image_tag_long: Annotated[
            str, dagger.Doc("One tag of the deployed image with image name")
        ] = dagger.field()

    @function
    async def deploy_single_docker_image(
            self,
            image_name: Annotated[str, dagger.Doc("Name of the image to build and push")],
            context_dir: Annotated[
                str, dagger.Doc("Path to the Docker build context (in the repo)")
            ],
            docker_registry: Annotated[str, dagger.Doc("Docker registry to push to")],
            docker_registry_username: Annotated[str, dagger.Doc("Docker registry username")],
            docker_registry_pat: Annotated[
                str, dagger.Doc("Docker registry password or PAT")
            ],
            github_ref_before: Annotated[str, dagger.Doc("Github ref before the change")],
            github_ref_after: Annotated[str, dagger.Doc("Github ref after the change")],
            github_repository_pat: Annotated[
                Optional[str], dagger.Doc("PAT for GitHub diffing (optional)")
            ] = None,
            depends_on_paths: Annotated[
                Optional[List[str]], dagger.Doc("Paths that trigger a rebuild")
            ] = None,
            build_args: Annotated[
                Optional[Dict[str, str]], dagger.Doc("Build arguments for the Dockerfile")
            ] = None,
            docker_file_name: Annotated[
                str, dagger.Doc("Name of the Dockerfile")
            ] = "Dockerfile",
            force_rebuild: Annotated[
                bool, dagger.Doc("Forces rebuild even if no changes are detected")
            ] = False,
            platform: Annotated[
                str, dagger.Doc("Target platform to build for")
            ] = "linux/amd64",
    ) -> DeploySingleDockerImageResult:
        """Build and deploy image"""

        rebuild = force_rebuild

        # Check for changes if not forced to rebuild
        if not force_rebuild and github_ref_before and github_ref_after:
            source_repo = dag.git(
                "https://github.com/"
                + os.environ.get("GITHUB_REPOSITORY", "some-owner/some-repo"),
                keep_git_dir=True,
            )
            if github_repository_pat:
                source_repo = source_repo.with_auth(
                    os.environ.get("GITHUB_ACTOR", "github-actions"),
                    github_repository_pat,
                )

            try:
                # Validate github_ref_before and github_ref_after
                await source_repo.with_exec(
                    ["git", "cat-file", "-e", f"{github_ref_before}^{{commit}}"]
                )
                await source_repo.with_exec(
                    ["git", "cat-file", "-e", f"{github_ref_after}^{{commit}}"]
                )
            except dagger.ExecError:
                print("Validation failed for Git refs. Forcing rebuild.")
                rebuild = True

            if not rebuild:
                all_paths: List[str] = list(depends_on_paths or [])
                if context_dir and context_dir not in all_paths:
                    all_paths.append(context_dir)

                # Ensure each path exists (file or directory)
                for path in all_paths:
                    if await source_repo.directory(path).exists() or await source_repo.file(
                            path
                    ).exists():
                        continue
                    print(f"Path {path} does not exist in repo; rebuilding to be safe.")
                    rebuild = True
                    break

                if not rebuild:
                    diff_output = await source_repo.with_exec(
                        ["git", "diff", "--name-only", github_ref_before, github_ref_after]
                    ).stdout()
                    changed_files = diff_output.splitlines()

                    def path_matches(file_path: str, watched: List[str]) -> bool:
                        for p in watched:
                            p = p.rstrip("/")
                            if file_path == p or file_path.startswith(p + "/"):
                                return True
                        return False

                    if any(path_matches(f, all_paths) for f in changed_files):
                        print("Changes detected. Rebuilding image.")
                        rebuild = True
                    else:
                        print("No changes detected. Skipping build.")
                        rebuild = False

        if not rebuild:
            return self.DeploySingleDockerImageResult(
                rebuild=False,
                image_tags=[],
                one_image_tag_short="",
                one_image_tag_long="",
                container=dag.container(),
                exit_code=0,
            )

        # Build and push the image
        source = dag.host().directory(".")
        context_path = context_dir
        dockerfile_path = os.path.join(context_path, docker_file_name)

        if not await source.file(dockerfile_path).exists():
            dockerfile_path = os.path.join(context_path, ".devcontainer", docker_file_name)
            if not await source.file(dockerfile_path).exists():
                raise FileNotFoundError(
                    f"No Dockerfile found for image {image_name} in {context_dir} or {context_dir}/.devcontainer!"
                )
            context_path = os.path.join(context_path, ".devcontainer")

        print(
            f"Building image from context: {context_path} with Dockerfile: {docker_file_name}"
        )

        # Construct image tags
        org = os.environ.get("GITHUB_REPOSITORY_OWNER", "some-owner")
        ref_name = os.environ.get("GITHUB_REF_NAME", "latest")
        sha_short = os.environ.get("GITHUB_SHA", "latest")[:7]
        image_base = f"{docker_registry}/{org}/{image_name}"

        image_tags = [
            f"{image_base}:{ref_name}",
            f"{image_base}:{sha_short}",
        ]
        if os.environ.get("GITHUB_REF_TYPE") == "tag":
            image_tags.append(
                f"{image_base}:{os.environ.get('GITHUB_REF_NAME', 'v1.0.0').lstrip('v')}"
            )

        # Build image
        build_ctr = dag.container().build(
            context=source.directory(context_path),
            dockerfile=docker_file_name,
            platform=platform,
            build_args=[
                dagger.BuildArg(name=k, value=v) for k, v in (build_args or {}).items()
            ],
        )

        # Push image
        pushed_ref = await build_ctr.with_registry_auth(
            docker_registry, docker_registry_username, docker_registry_pat
        ).publish(image_base, tags=image_tags)

        print(f"Successfully pushed image: {pushed_ref}")

        # Extract one image tag
        tags_short = [tag.split(":")[-1] for tag in image_tags]
        tag_short = next((t for t in tags_short if t.startswith("v")), None)
        if tag_short is None:
            tag_short = next(
                (t for t in tags_short if not t.startswith("latest")),
                tags_short[0] if tags_short else "latest",
            )
        tag_long = next((t for t in image_tags if t.endswith(":" + tag_short)), "")

        print(f"Extracted short tag: {tag_short}")
        print(f"Extracted long tag: {tag_long}")

        return self.DeploySingleDockerImageResult(
            rebuild=True,
            image_tags=image_tags,
            one_image_tag_short=tag_short,
            one_image_tag_long=tag_long,
            container=build_ctr,
            exit_code=0,
        )
